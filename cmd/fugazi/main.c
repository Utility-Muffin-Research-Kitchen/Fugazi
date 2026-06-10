/*
 * Fugazi — CRT shader tweaker with live preview, native Leaf app.
 *
 * Catastrophe UI (the Leaf toolkit) + OpenGL ES 2.0 shader preview. The shader
 * effect engine (2-pass glow + scanline/mask/vignette through GLES) is the same
 * CRT model as the original NextUI Fugazi; the app shell, UI, input loop and
 * packaging are written natively for Leaf/Catastrophe.
 *
 * Controls:
 *   Up/Down     cycle parameters
 *   Left/Right  adjust the selected parameter
 *   X           toggle game image vs built-in test pattern
 *   Y           clear all effects (passthrough)
 *   A           install to system (RetroArch) — Phase 2
 *   B           quit
 *
 * Render path: GLES shader pass -> FBO -> glReadPixels -> SDL_Texture ->
 * cat_draw_image fullscreen, with the Catastrophe UI drawn on top. The readback
 * decouples the raw-GL pass from Catastrophe's SDL_Renderer (which is GLES2-
 * backed on MLP1), so the two compose without fighting over GL state.
 */

#define CAT_IMPLEMENTATION
#include "catastrophe.h"
#define CAT_WIDGETS_IMPLEMENTATION
#include "catastrophe_widgets.h"

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <SDL2/SDL_image.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_PATH_LEN 1280
#define MAX_PARAMS   8

typedef struct {
    const char *name;        /* matches the GLSL uniform name */
    const char *label;       /* display label */
    const char *description; /* short explanation */
    float       value;
    float       clear_val;   /* "no visible effect" value */
    float       min, max, step;
    int         shader;      /* 0 = glow pass, 1 = scanline pass */
} fugazi_param;

typedef struct {
    fugazi_param params[MAX_PARAMS];
    int          param_count;
    int          cursor;

    char         pak_dir[MAX_PATH_LEN];

    SDL_Texture *preview_texture;
    GLuint       gl_program_glow;
    GLuint       gl_program_scan;
    GLuint       gl_source_tex;
    GLuint       gl_test_tex;
    GLuint       gl_fbo;
    GLuint       gl_fbo_tex;
    GLuint       gl_output_tex;
    int          preview_w, preview_h;
    int          source_w, source_h;
    int          test_w, test_h;
    int          sim_input_w, sim_input_h;
    int          gl_initialized;
    int          use_test_pattern;
} fugazi_state;

static fugazi_state state;

/* ------------------------------------------------------------------ params */

static void init_params(void)
{
    int i = 0;
    state.params[i++] = (fugazi_param){ .name="CURVATURE", .label="Curvature",
        .description="Screen edge bend", .value=0.06f, .clear_val=0.0f,
        .min=0.0f, .max=0.25f, .step=0.01f, .shader=0 };
    state.params[i++] = (fugazi_param){ .name="GLOW_MIX", .label="Glow",
        .description="Soft light bleed", .value=0.35f, .clear_val=0.0f,
        .min=0.0f, .max=0.8f, .step=0.05f, .shader=0 };
    state.params[i++] = (fugazi_param){ .name="SCANLINE_WEIGHT", .label="Scanlines",
        .description="Dark line strength", .value=0.55f, .clear_val=0.0f,
        .min=0.0f, .max=1.0f, .step=0.05f, .shader=1 };
    state.params[i++] = (fugazi_param){ .name="SCANLINE_GAP", .label="Gap Darkness",
        .description="Floor between lines", .value=0.55f, .clear_val=1.0f,
        .min=0.0f, .max=1.0f, .step=0.05f, .shader=1 };
    state.params[i++] = (fugazi_param){ .name="MASK_STRENGTH", .label="Phosphor Mask",
        .description="Vertical grille lines", .value=0.30f, .clear_val=0.0f,
        .min=0.0f, .max=0.6f, .step=0.05f, .shader=1 };
    state.params[i++] = (fugazi_param){ .name="VIGNETTE", .label="Vignette",
        .description="Edge darkening", .value=0.20f, .clear_val=0.0f,
        .min=0.0f, .max=0.6f, .step=0.05f, .shader=1 };
    state.params[i++] = (fugazi_param){ .name="BRIGHTNESS", .label="Brightness",
        .description="Output gain", .value=1.10f, .clear_val=1.0f,
        .min=0.5f, .max=1.6f, .step=0.05f, .shader=1 };
    state.params[i++] = (fugazi_param){ .name="WARMTH", .label="Warmth",
        .description="Warm color tint", .value=0.05f, .clear_val=0.0f,
        .min=0.0f, .max=0.3f, .step=0.02f, .shader=1 };
    state.param_count = i;
}

static float get_param_value(const char *name)
{
    for (int i = 0; i < state.param_count; i++)
        if (strcmp(state.params[i].name, name) == 0) return state.params[i].value;
    return 0.0f;
}

static void clear_all_params(void)
{
    for (int i = 0; i < state.param_count; i++)
        state.params[i].value = state.params[i].clear_val;
}

/* ----------------------------------------------------------- GLES engine */

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        cat_log("fugazi: shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vert, GLuint frag)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        cat_log("fugazi: program link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static const char *preview_vert_src =
    "#version 100\n"
    "precision mediump float;\n"
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texcoord;\n"
    "varying vec2 TEX0;\n"
    "uniform vec2 TextureSize;\n"
    "varying vec2 v_offset_x;\n"
    "varying vec2 v_offset_y;\n"
    "void main() {\n"
    "    gl_Position = a_position;\n"
    "    TEX0 = a_texcoord;\n"
    "    v_offset_x = vec2(2.0 / TextureSize.x, 0.0);\n"
    "    v_offset_y = vec2(0.0, 2.0 / TextureSize.y);\n"
    "}\n";

static const char *preview_glow_frag_src =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D Texture;\n"
    "uniform vec2 InputSize;\n"
    "uniform vec2 TextureSize;\n"
    "uniform float CURVATURE;\n"
    "uniform float GLOW_MIX;\n"
    "varying vec2 TEX0;\n"
    "varying vec2 v_offset_x;\n"
    "varying vec2 v_offset_y;\n"
    "void main() {\n"
    "    vec2 scale = TextureSize / InputSize;\n"
    "    vec2 tex0 = TEX0 * scale;\n"
    "    vec2 centered = tex0 - vec2(0.5);\n"
    "    float r2 = dot(centered, centered);\n"
    "    centered *= 1.0 + CURVATURE * r2;\n"
    "    vec2 edge = step(vec2(-0.5), centered) * step(centered, vec2(0.5));\n"
    "    float inside = edge.x * edge.y;\n"
    "    vec2 uv = (centered + vec2(0.5)) / scale;\n"
    "    vec3 center = texture2D(Texture, uv).rgb;\n"
    "    vec3 blur = texture2D(Texture, uv + v_offset_x).rgb;\n"
    "    blur += texture2D(Texture, uv - v_offset_x).rgb;\n"
    "    blur += texture2D(Texture, uv + v_offset_y).rgb;\n"
    "    blur += texture2D(Texture, uv - v_offset_y).rgb;\n"
    "    blur *= 0.25;\n"
    "    vec3 color = mix(center, blur, GLOW_MIX);\n"
    "    gl_FragColor = vec4(color * inside, 1.0);\n"
    "}\n";

static const char *preview_scan_frag_src =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D Texture;\n"
    "uniform vec2 InputSize;\n"
    "uniform vec2 TextureSize;\n"
    "uniform vec2 OutputSize;\n"
    "uniform float SCANLINE_WEIGHT;\n"
    "uniform float SCANLINE_GAP;\n"
    "uniform float MASK_STRENGTH;\n"
    "uniform float VIGNETTE;\n"
    "uniform float BRIGHTNESS;\n"
    "uniform float WARMTH;\n"
    "varying vec2 TEX0;\n"
    "void main() {\n"
    "    vec3 color = texture2D(Texture, TEX0).rgb;\n"
    "    float luma = dot(color, vec3(0.299, 0.587, 0.114));\n"
    "    float scanline_pos = TEX0.y * InputSize.y;\n"
    "    float frac_y = fract(scanline_pos);\n"
    "    float dist = abs(frac_y - 0.5) * 2.0;\n"
    "    float adaptive_weight = SCANLINE_WEIGHT * (1.0 - luma * 0.5);\n"
    "    float scanline = 1.0 - dist * dist * adaptive_weight;\n"
    "    scanline = max(scanline, SCANLINE_GAP);\n"
    "    color *= scanline;\n"
    "    float mask_fade = smoothstep(0.05, 0.25, luma);\n"
    "    float effective_mask = MASK_STRENGTH * mask_fade;\n"
    "    float gx = abs(fract(gl_FragCoord.x / 3.0) - 0.5) * 2.0;\n"
    "    float m = 1.0 - effective_mask * gx;\n"
    "    color *= m;\n"
    "    color.r *= 1.0 + WARMTH;\n"
    "    color.g *= 1.0 + WARMTH * 0.4;\n"
    "    color.b *= 1.0 - WARMTH * 0.6;\n"
    "    color *= BRIGHTNESS;\n"
    "    vec2 vig_uv = TEX0 * 2.0 - 1.0;\n"
    "    float vig = 1.0 - dot(vig_uv, vig_uv) * VIGNETTE;\n"
    "    color *= vig;\n"
    "    color = min(color, vec3(1.0));\n"
    "    gl_FragColor = vec4(color, 1.0);\n"
    "}\n";

static const float quad_verts[] = {
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
};

static void set_uniform_float(GLuint prog, const char *name, float val)
{
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform1f(loc, val);
}
static void set_uniform_vec2(GLuint prog, const char *name, float x, float y)
{
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

static unsigned char *generate_test_pattern(int w, int h)
{
    unsigned char *pixels = calloc((size_t)w * h * 4, 1);
    if (!pixels) return NULL;
    #define TP_SET(px, py, r, g, b) do { \
        if ((px) >= 0 && (px) < w && (py) >= 0 && (py) < h) { \
            int _idx = ((py) * w + (px)) * 4; \
            pixels[_idx]=(unsigned char)(r); pixels[_idx+1]=(unsigned char)(g); \
            pixels[_idx+2]=(unsigned char)(b); pixels[_idx+3]=255; } \
    } while(0)

    int bar_h = h / 4;
    unsigned char bars[][3] = {
        {255,255,255},{255,255,0},{0,255,255},{0,255,0},
        {255,0,255},{255,0,0},{0,0,255},{0,0,0} };
    for (int y = 0; y < bar_h; y++)
        for (int x = 0; x < w; x++) {
            int col = (x * 8) / w; if (col > 7) col = 7;
            TP_SET(x, y, bars[col][0], bars[col][1], bars[col][2]);
        }

    int grid_y0 = bar_h, grid_y1 = (h * 60) / 100;
    int grid_spacing = w / 32; if (grid_spacing < 8) grid_spacing = 8;
    for (int y = grid_y0; y < grid_y1; y++)
        for (int x = 0; x < w; x++) TP_SET(x, y, 15, 15, 30);
    for (int y = grid_y0; y < grid_y1; y++)
        for (int x = 0; x < w; x++) {
            int on_v = ((x % grid_spacing) == 0);
            int on_h = (((y - grid_y0) % grid_spacing) == 0);
            if (on_v || on_h) TP_SET(x, y, (on_v&&on_h)?200:80, (on_v&&on_h)?200:80, (on_v&&on_h)?255:160);
        }

    int grad_y0 = grid_y1, grad_y1 = (h * 78) / 100;
    for (int y = grad_y0; y < grad_y1; y++)
        for (int x = 0; x < w; x++) { int v = (x * 255) / (w - 1); TP_SET(x, y, v, v, v); }

    int glow_y0 = grad_y1, cx = w / 2, cy = (glow_y0 + h) / 2;
    int radius = (h - glow_y0) / 3;
    for (int y = glow_y0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int dx = x - cx, dy = y - cy, d2 = dx*dx + dy*dy, r2 = radius*radius;
            if (d2 < r2) { float t = 1.0f - (float)d2/(float)r2; int v = (int)(255*t*t); TP_SET(x, y, v, v, v); }
        }

    int mark = w / 20;
    for (int y = 0; y < mark; y++)
        for (int x = 0; x < mark; x++) {
            TP_SET(x, y, 255,128,0);
            TP_SET(w-1-x, y, 0,128,255);
            TP_SET(x, h-1-y, 0,255,128);
            TP_SET(w-1-x, h-1-y, 255,128,255);
        }
    #undef TP_SET
    return pixels;
}

static int init_gl_preview(const char *image_path)
{
    SDL_Surface *img = IMG_Load(image_path);
    if (!img) { cat_log("fugazi: failed to load preview image: %s", image_path); return -1; }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(img);
    if (!rgba) return -1;

    state.source_w = state.preview_w = rgba->w;
    state.source_h = state.preview_h = rgba->h;
    state.sim_input_w = 320;  /* simulated low-res emulator output */
    state.sim_input_h = 224;

    glGenTextures(1, &state.gl_source_tex);
    glBindTexture(GL_TEXTURE_2D, state.gl_source_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);

    glGenTextures(1, &state.gl_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, state.gl_fbo_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &state.gl_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, state.gl_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.gl_fbo_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenTextures(1, &state.gl_output_tex);
    glBindTexture(GL_TEXTURE_2D, state.gl_output_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    SDL_FreeSurface(rgba);

    GLuint vert = compile_shader(GL_VERTEX_SHADER, preview_vert_src);
    GLuint frag_glow = compile_shader(GL_FRAGMENT_SHADER, preview_glow_frag_src);
    GLuint frag_scan = compile_shader(GL_FRAGMENT_SHADER, preview_scan_frag_src);
    if (!vert || !frag_glow || !frag_scan) return -1;
    state.gl_program_glow = link_program(vert, frag_glow);
    state.gl_program_scan = link_program(vert, frag_scan);
    glDeleteShader(vert); glDeleteShader(frag_glow); glDeleteShader(frag_scan);
    if (!state.gl_program_glow || !state.gl_program_scan) return -1;

    state.test_w = state.source_w; state.test_h = state.source_h;
    unsigned char *tp = generate_test_pattern(state.test_w, state.test_h);
    if (tp) {
        glGenTextures(1, &state.gl_test_tex);
        glBindTexture(GL_TEXTURE_2D, state.gl_test_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.test_w, state.test_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tp);
        free(tp);
    }
    state.gl_initialized = 1;
    return 0;
}

static void render_preview(void)
{
    if (!state.gl_initialized) return;
    float tex_w = (float)state.source_w, tex_h = (float)state.source_h;
    float input_w = (float)state.sim_input_w, input_h = (float)state.sim_input_h;

    /* Pass 1: glow -> FBO (gl_fbo_tex) */
    glBindFramebuffer(GL_FRAMEBUFFER, state.gl_fbo);
    glViewport(0, 0, state.preview_w, state.preview_h);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(state.gl_program_glow);
    GLint pos_loc = glGetAttribLocation(state.gl_program_glow, "a_position");
    GLint tex_loc = glGetAttribLocation(state.gl_program_glow, "a_texcoord");
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 16, quad_verts);
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, 16, quad_verts + 2);
    glEnableVertexAttribArray(tex_loc);
    glActiveTexture(GL_TEXTURE0);
    GLuint active_tex = (state.use_test_pattern && state.gl_test_tex) ? state.gl_test_tex : state.gl_source_tex;
    glBindTexture(GL_TEXTURE_2D, active_tex);
    glUniform1i(glGetUniformLocation(state.gl_program_glow, "Texture"), 0);
    set_uniform_vec2(state.gl_program_glow, "InputSize", tex_w, tex_h);
    set_uniform_vec2(state.gl_program_glow, "TextureSize", tex_w, tex_h);
    set_uniform_float(state.gl_program_glow, "CURVATURE", get_param_value("CURVATURE"));
    set_uniform_float(state.gl_program_glow, "GLOW_MIX", get_param_value("GLOW_MIX"));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Pass 2: scanline -> output texture */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.gl_output_tex, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(state.gl_program_scan);
    pos_loc = glGetAttribLocation(state.gl_program_scan, "a_position");
    tex_loc = glGetAttribLocation(state.gl_program_scan, "a_texcoord");
    glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 16, quad_verts);
    glEnableVertexAttribArray(pos_loc);
    glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, 16, quad_verts + 2);
    glEnableVertexAttribArray(tex_loc);
    glBindTexture(GL_TEXTURE_2D, state.gl_fbo_tex);
    glUniform1i(glGetUniformLocation(state.gl_program_scan, "Texture"), 0);
    set_uniform_vec2(state.gl_program_scan, "InputSize", input_w, input_h);
    set_uniform_vec2(state.gl_program_scan, "TextureSize", tex_w, tex_h);
    set_uniform_vec2(state.gl_program_scan, "OutputSize", (float)state.preview_w, (float)state.preview_h);
    set_uniform_float(state.gl_program_scan, "SCANLINE_WEIGHT", get_param_value("SCANLINE_WEIGHT"));
    set_uniform_float(state.gl_program_scan, "SCANLINE_GAP", get_param_value("SCANLINE_GAP"));
    set_uniform_float(state.gl_program_scan, "MASK_STRENGTH", get_param_value("MASK_STRENGTH"));
    set_uniform_float(state.gl_program_scan, "VIGNETTE", get_param_value("VIGNETTE"));
    set_uniform_float(state.gl_program_scan, "BRIGHTNESS", get_param_value("BRIGHTNESS"));
    set_uniform_float(state.gl_program_scan, "WARMTH", get_param_value("WARMTH"));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.gl_fbo_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static SDL_Texture *get_preview_as_sdl_texture(SDL_Renderer *renderer)
{
    if (!state.gl_initialized) return NULL;
    int w = state.preview_w, h = state.preview_h;
    unsigned char *pixels = malloc((size_t)w * h * 4);
    if (!pixels) return NULL;
    glBindFramebuffer(GL_FRAMEBUFFER, state.gl_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.gl_output_tex, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.gl_fbo_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!state.preview_texture) {
        state.preview_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STREAMING, w, h);
    }
    if (state.preview_texture) SDL_UpdateTexture(state.preview_texture, NULL, pixels, w * 4);
    free(pixels);
    return state.preview_texture;
}

/* --------------------------------------------------------- install (Phase 2)

   Bake the live-tuned values into a RetroArch GLSL preset and point
   retroarch.cfg at it. Whatever the user dialed in here is what RetroArch
   shows — no need to tweak shader parameters in RetroArch afterward. */

/* mkdir -p: create each path component, ignoring already-exists. */
static void mkdir_p(const char *path)
{
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* snprintf into a fixed buffer; returns 0 on success, -1 on truncation/error.
   Checking the result keeps the build warning-clean under -Wformat-truncation
   and surfaces an over-long path instead of silently using a truncated one. */
static int fz_path(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return (r >= 0 && (size_t)r < n) ? 0 : -1;
}

/* Resolve RetroArch's state dir (…/state/retroarch) from the Leaf env. */
static void resolve_ra_dir(char *out, size_t out_sz)
{
    const char *state = getenv("UMRK_INTERNAL_DATA_PATH");
    if (state && state[0]) { snprintf(out, out_sz, "%s/retroarch", state); return; }
    const char *plat = getenv("UMRK_PLATFORM_PATH");
    if (!plat || !plat[0]) plat = getenv("SYSTEM_PATH");
    if (plat && plat[0]) { snprintf(out, out_sz, "%s/state/retroarch", plat); return; }
    const char *sd = getenv("SDCARD_PATH");
    if (!sd || !sd[0]) sd = "/mnt/sdcard";
    snprintf(out, out_sz, "%s/.system/leaf/platforms/mlp1/state/retroarch", sd);
}

/* Copy a template shader, rewriting each tuned param's #define with the live
   value (param names match the shader #define names 1:1). */
static int bake_shader(const char *src, const char *dst)
{
    FILE *in = fopen(src, "r");
    if (!in) return -1;
    FILE *out = fopen(dst, "w");
    if (!out) { fclose(in); return -1; }
    char line[1024];
    while (fgets(line, sizeof(line), in)) {
        char name[64];
        if (sscanf(line, " #define %63s", name) == 1) {
            int baked = 0;
            for (int i = 0; i < state.param_count; i++) {
                if (strcmp(state.params[i].name, name) == 0) {
                    fprintf(out, "#define %s %.5f\n", name, state.params[i].value);
                    baked = 1;
                    break;
                }
            }
            if (baked) continue;
        }
        fputs(line, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

/* Two-pass GLSL preset: glow at source res, scanline/mask at viewport res. */
static int write_glslp(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs("shaders = 2\n\n"
          "shader0 = fugazi-glow.glsl\n"
          "filter_linear0 = true\n"
          "scale_type0 = source\n"
          "scale0 = 1.000000\n\n"
          "shader1 = fugazi-scanline.glsl\n"
          "filter_linear1 = false\n"
          "scale_type1 = viewport\n"
          "scale1 = 1.000000\n", f);
    fclose(f);
    return 0;
}

/* Point retroarch.cfg at our preset and ensure shaders are enabled. */
static int set_video_shader(const char *cfg_path, const char *glslp_path)
{
    char tmp[MAX_PATH_LEN];
    if (fz_path(tmp, sizeof(tmp), "%s.fugazi.tmp", cfg_path) != 0) return -1;
    FILE *out = fopen(tmp, "w");
    if (!out) return -1;
    FILE *in = fopen(cfg_path, "r");
    int seen_shader = 0, seen_enable = 0;
    char line[1024];
    if (in) {
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, "video_shader ", 13) == 0 ||
                strncmp(line, "video_shader=", 13) == 0) {
                fprintf(out, "video_shader = \"%s\"\n", glslp_path);
                seen_shader = 1;
            } else if (strncmp(line, "video_shader_enable", 19) == 0) {
                fputs("video_shader_enable = \"true\"\n", out);
                seen_enable = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    if (!seen_shader) fprintf(out, "video_shader = \"%s\"\n", glslp_path);
    if (!seen_enable) fputs("video_shader_enable = \"true\"\n", out);
    fclose(out);
    return rename(tmp, cfg_path);
}

/* Locate RetroArch's menu config dir (where it looks for automatic presets).
   RA does NOT auto-load the global `video_shader` cfg value at boot -- it only
   loads an "automatic preset" (game/core/content-dir/global) from this dir.
   The Leaf env-contract publishes it as UMRK_RETROARCH_CONFIG_DIR (env.sh).
   Fallback derives it the same way jawakad does: RA is launched with HOME =
   the SD state dir (== resolve_ra_dir), so its config tree is
   <state>/retroarch/.config/retroarch/config. */
static void resolve_ra_config_dir(char *out, size_t out_sz)
{
    const char *env = getenv("UMRK_RETROARCH_CONFIG_DIR");
    if (env && env[0]) { snprintf(out, out_sz, "%s", env); return; }

    char ra_dir[MAX_PATH_LEN];
    resolve_ra_dir(ra_dir, sizeof(ra_dir));
    fz_path(out, out_sz, "%s/.config/retroarch/config", ra_dir);
}

/* Write RA's GLOBAL automatic preset as a one-line #reference to our preset.
   With auto_shaders_enable=true (RA default), RA auto-loads this for every core
   on content launch -- the actual mechanism that makes the shader auto-apply. */
static int write_global_preset(const char *path, const char *ref_glslp)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "#reference \"%s\"\n", ref_glslp);
    fclose(f);
    return 0;
}

static void install_to_system(void)
{
    char ra_dir[MAX_PATH_LEN], shader_dir[MAX_PATH_LEN];
    char glslp[MAX_PATH_LEN], cfg[MAX_PATH_LEN];
    char src_glow[MAX_PATH_LEN], src_scan[MAX_PATH_LEN];
    char dst_glow[MAX_PATH_LEN], dst_scan[MAX_PATH_LEN];
    char ra_cfg_dir[MAX_PATH_LEN], global_preset[MAX_PATH_LEN];

    resolve_ra_dir(ra_dir, sizeof(ra_dir));
    resolve_ra_config_dir(ra_cfg_dir, sizeof(ra_cfg_dir));

    const char *msg;
    if (fz_path(shader_dir, sizeof(shader_dir), "%s/.config/retroarch/shaders/fugazi", ra_dir) != 0 ||
        fz_path(cfg, sizeof(cfg), "%s/retroarch.cfg", ra_dir) != 0 ||
        fz_path(glslp, sizeof(glslp), "%s/fugazi.glslp", shader_dir) != 0 ||
        fz_path(src_glow, sizeof(src_glow), "%s/shaders/fugazi-glow.glsl", state.pak_dir) != 0 ||
        fz_path(src_scan, sizeof(src_scan), "%s/shaders/fugazi-scanline.glsl", state.pak_dir) != 0 ||
        fz_path(dst_glow, sizeof(dst_glow), "%s/fugazi-glow.glsl", shader_dir) != 0 ||
        fz_path(dst_scan, sizeof(dst_scan), "%s/fugazi-scanline.glsl", shader_dir) != 0 ||
        fz_path(global_preset, sizeof(global_preset), "%s/global.glslp", ra_cfg_dir) != 0) {
        cat_log("fugazi: install failed - a system path was too long");
        msg = "Install failed: a system path was too long.";
    } else {
        mkdir_p(shader_dir);
        if (bake_shader(src_glow, dst_glow) != 0 || bake_shader(src_scan, dst_scan) != 0) {
            cat_log("fugazi: install failed baking shaders (pak_dir=%s)", state.pak_dir);
            msg = "Install failed: couldn't write the shader files.";
        } else if (write_glslp(glslp) != 0) {
            msg = "Install failed: couldn't write the preset.";
        } else if ((mkdir_p(ra_cfg_dir), write_global_preset(global_preset, glslp)) != 0) {
            cat_log("fugazi: shaders written but global preset failed (%s)", global_preset);
            msg = "Shader saved, but enabling it in RetroArch failed.";
        } else {
            /* Best-effort: also set the menu's last-loaded preset (cosmetic; RA does
               not auto-load this at boot -- the global preset above is what applies). */
            set_video_shader(cfg, glslp);
            cat_log("fugazi: installed shader -> %s (global preset %s)", glslp, global_preset);
            msg = "Installed. Your CRT shader is now active in\nRetroArch — launch a game to see it.";
        }
    }

    cat_footer_item footer[1] = {
        { .button = CAT_BTN_A, .label = "OK", .is_confirm = true },
    };
    cat_message_opts opts = {
        .message = msg, .footer = footer, .footer_count = 1,
    };
    cat_confirm_result r;
    cat_confirmation(&opts, &r);
}

/* --------------------------------------------------------------------- main */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    memset(&state, 0, sizeof(state));

    const char *pak_dir = getenv("FUGAZI_PAK_DIR");
    if (!pak_dir || !pak_dir[0]) pak_dir = ".";
    snprintf(state.pak_dir, sizeof(state.pak_dir), "%s", pak_dir);

    init_params();

    cat_config cfg = {
        .window_title = "Fugazi",
        .log_path     = cat_resolve_log_path("fugazi"),
        .cpu_speed    = CAT_CPU_SPEED_NORMAL,
    };
    if (cat_init(&cfg) != CAT_OK) {
        fprintf(stderr, "Fugazi: failed to initialise Catastrophe\n");
        return 1;
    }
    cat_log("fugazi: startup, pak_dir=%s", state.pak_dir);

    char preview_path[MAX_PATH_LEN];
    if (fz_path(preview_path, sizeof(preview_path), "%s/res/preview.png", state.pak_dir) != 0)
        cat_log("fugazi: preview path too long - running without preview");
    else if (init_gl_preview(preview_path) != 0)
        cat_log("fugazi: GL preview init failed - running without preview");

    SDL_Renderer *renderer = cat_get_renderer();

    int running = 1;
    while (running) {
        cat_input_event ev;
        while (cat_poll_input(&ev)) {
            if (!ev.pressed) continue;
            switch (ev.button) {
                case CAT_BTN_B:    if (!ev.repeated) running = 0; break;
                case CAT_BTN_UP:   state.cursor = (state.cursor - 1 + state.param_count) % state.param_count; break;
                case CAT_BTN_DOWN: state.cursor = (state.cursor + 1) % state.param_count; break;
                case CAT_BTN_LEFT: {
                    fugazi_param *p = &state.params[state.cursor];
                    p->value -= p->step; if (p->value < p->min) p->value = p->min;
                    break; }
                case CAT_BTN_RIGHT: {
                    fugazi_param *p = &state.params[state.cursor];
                    p->value += p->step; if (p->value > p->max) p->value = p->max;
                    break; }
                case CAT_BTN_A: if (!ev.repeated) install_to_system(); break;
                case CAT_BTN_Y: if (!ev.repeated) clear_all_params(); break;
                case CAT_BTN_X: if (!ev.repeated) state.use_test_pattern = !state.use_test_pattern; break;
                default: break;
            }
        }
        cat_request_frame();   /* live tuner: keep rendering */

        if (state.gl_initialized) {
            render_preview();
            glViewport(0, 0, cat_get_screen_width(), cat_get_screen_height());
        }

        cat_clear_screen();
        int sw = cat_get_screen_width();
        int sh = cat_get_screen_height();
        int pad = CAT_S(6);

        TTF_Font *font_small = cat_get_font(CAT_FONT_SMALL);
        ap_theme *theme = cat_get_theme();

        /* Honor the launcher's button-hints setting (exported as CAT_SHOW_HINTS).
           The bar holds the parameter line stacked ABOVE the footer, so the hints
           never cover the parameter "menu" line; with hints off, drop the footer
           and shrink the bar to just the parameter line. */
        bool show_hints  = cat_hints_enabled_from_env();
        int footer_h     = show_hints ? cat_get_footer_height() : 0;
        int param_line_h = TTF_FontHeight(font_small);
        int bar_h        = footer_h + param_line_h + pad * 2;

        /* full-screen preview, aspect-fit */
        if (state.gl_initialized) {
            SDL_Texture *tex = get_preview_as_sdl_texture(renderer);
            if (tex) {
                float aspect = (float)state.preview_w / (float)state.preview_h;
                int dst_w = sw, dst_h = (int)(dst_w / aspect);
                if (dst_h > sh) { dst_h = sh; dst_w = (int)(dst_h * aspect); }
                SDL_Rect dst = { (sw - dst_w) / 2, (sh - dst_h) / 2, dst_w, dst_h };
                SDL_RenderCopy(renderer, tex, NULL, &dst);
            }
        } else {
            const char *msg = "Preview not available";
            int mw = cat_measure_text(font_small, msg);
            cat_draw_text(font_small, msg, (sw - mw) / 2, sh / 2, theme->hint);
        }

        /* semi-transparent bottom bar */
        int bar_y = sh - bar_h;
        cat_draw_rect(0, bar_y, sw, bar_h, (ap_color){ 0, 0, 0, 180 });

        /* parameter line: < Label — desc           value > */
        {
            fugazi_param *p = &state.params[state.cursor];
            int py = bar_y + pad;
            char val_str[32];
            snprintf(val_str, sizeof(val_str), "%.2f", p->value);
            int arrow_w = cat_measure_text(font_small, "<");
            cat_draw_text(font_small, "<", pad * 2, py, theme->highlight);
            char label_buf[160];
            snprintf(label_buf, sizeof(label_buf), "%s — %s", p->label, p->description);
            cat_draw_text(font_small, label_buf, pad * 2 + arrow_w + pad, py, theme->text);
            int val_w = cat_measure_text(font_small, val_str);
            cat_draw_text(font_small, ">", sw - pad * 2 - arrow_w, py, theme->highlight);
            cat_draw_text(font_small, val_str, sw - pad * 2 - arrow_w - pad - val_w, py, theme->text);
        }

        /* footer hints (only when the launcher has hints enabled) */
        if (show_hints) {
            cat_footer_item footer[] = {
                { .button = CAT_BTN_B, .label = "Quit" },
                { .button = CAT_BTN_Y, .label = "Clear" },
                { .button = CAT_BTN_X, .label = state.use_test_pattern ? "Game Image" : "Test Pattern" },
                { .button = CAT_BTN_A, .label = "Install", .is_confirm = true },
            };
            cat_draw_footer(footer, 4);
        }

        cat_present();
    }

    if (state.preview_texture) SDL_DestroyTexture(state.preview_texture);
    if (state.gl_initialized) {
        glDeleteProgram(state.gl_program_glow);
        glDeleteProgram(state.gl_program_scan);
        glDeleteTextures(1, &state.gl_source_tex);
        if (state.gl_test_tex) glDeleteTextures(1, &state.gl_test_tex);
        glDeleteTextures(1, &state.gl_fbo_tex);
        glDeleteTextures(1, &state.gl_output_tex);
        glDeleteFramebuffers(1, &state.gl_fbo);
    }
    cat_quit();
    return 0;
}
