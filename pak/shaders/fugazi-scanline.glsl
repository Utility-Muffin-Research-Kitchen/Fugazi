// Fugazi CRT - Pass 2: Scanlines + RGB phosphor mask + vignette
// Optimized for Mali-G31 (TrimUI Brick, 1024x768)
// Brightness-adaptive scanlines: bright pixels bleed, dark pixels stay thin

#if defined(VERTEX)

uniform mediump mat4 MVPMatrix;
attribute mediump vec4 VertexCoord;
attribute mediump vec2 TexCoord;

varying mediump vec2 TEX0;

void main()
{
    // No Y-flip — match RA's stock convention (see fugazi-glow.glsl).
    TEX0 = TexCoord;
    gl_Position = MVPMatrix * VertexCoord;
}

#elif defined(FRAGMENT)


precision mediump float;
precision mediump int;
uniform sampler2D Texture;
uniform vec2 InputSize;
uniform vec2 TextureSize;
uniform vec2 OutputSize;

varying mediump vec2 TEX0;

#define SCANLINE_WEIGHT 0.55
#define SCANLINE_GAP 0.55
#define MASK_STRENGTH 0.30
#define VIGNETTE 0.20
#define BRIGHTNESS 1.10
#define WARMTH 0.05

void main()
{
    // Sample at the raw texcoord (content lives in [0, scale] of the padded
    // texture). Normalize to [0,1] content space for the scanline/mask/vignette
    // math so they line up with the visible image, not the padding.
    vec2 scale = InputSize / TextureSize;
    vec2 uv = TEX0 / scale;

    vec3 color = texture2D(Texture, TEX0).rgb;

    // Pixel luminance (cheap dot product, no sqrt)
    float luma = dot(color, vec3(0.299, 0.587, 0.114));

    // --- Brightness-adaptive scanlines ---
    // Bright pixels: scanlines are softer (beam blooms wider)
    // Dark pixels: scanlines are sharper (thin beam)
    float scanline_pos = uv.y * InputSize.y;
    float frac_y = fract(scanline_pos);

    float dist = abs(frac_y - 0.5) * 2.0;

    // Reduce scanline weight for bright pixels — simulates beam spread
    // luma=1 -> effective weight is halved, luma=0 -> full weight
    float adaptive_weight = SCANLINE_WEIGHT * (1.0 - luma * 0.5);
    float scanline = 1.0 - dist * dist * adaptive_weight;
    scanline = max(scanline, SCANLINE_GAP);

    color *= scanline;

    // --- Vertical aperture grille (scalar luminance) ---
    // A hard RGB floor/mod triad relies on pass1 rendering 1:1 with the panel;
    // it does not (RA scales + rotates the final pass), so integer phases land
    // unevenly and leave a color cast -- and a 1px RGB triad fights the LCD's
    // own subpixels on a 960px 4" panel, so the chroma reads as shimmer, not
    // color. A scalar grille dims all channels equally: cast-free by definition,
    // cheaper than the RGB mask, and the grille + scanlines still read as CRT.
    float mask_fade = smoothstep(0.05, 0.25, luma);
    float effective_mask = MASK_STRENGTH * mask_fade;

    float gx = abs(fract(gl_FragCoord.x / 3.0) - 0.5) * 2.0;  // 3px triangle grille
    float m = 1.0 - effective_mask * gx;

    color *= m;

    // --- Color warmth (CRT phosphor tint) ---
    // Slight warm shift: boost red/green, cut blue
    color.r *= 1.0 + WARMTH;
    color.g *= 1.0 + WARMTH * 0.4;
    color.b *= 1.0 - WARMTH * 0.6;

    // --- Brightness compensation ---
    // Scanlines + mask darken the image; this brings it back up
    color *= BRIGHTNESS;

    // --- Vignette ---
    vec2 vig_uv = uv * 2.0 - 1.0;
    float vig = 1.0 - dot(vig_uv, vig_uv) * VIGNETTE;
    color *= vig;

    // Clamp to prevent overbright from boost + glow
    color = min(color, vec3(1.0));

    gl_FragColor = vec4(color, 1.0);
}

#endif
