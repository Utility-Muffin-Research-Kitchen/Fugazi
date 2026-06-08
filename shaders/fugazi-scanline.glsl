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
    TEX0 = vec2(TexCoord.x, 1.0 - TexCoord.y);
    gl_Position = MVPMatrix * VertexCoord;
}

#elif defined(FRAGMENT)

uniform sampler2D Texture;
uniform vec2 InputSize;
uniform vec2 TextureSize;
uniform vec2 OutputSize;

varying mediump vec2 TEX0;

#define SCANLINE_WEIGHT 0.85
#define SCANLINE_GAP 0.10
#define MASK_STRENGTH 0.35
#define VIGNETTE 0.25
#define BRIGHTNESS 1.45
#define WARMTH 0.06

void main()
{
    vec3 color = texture2D(Texture, TEX0).rgb;

    // Pixel luminance (cheap dot product, no sqrt)
    float luma = dot(color, vec3(0.299, 0.587, 0.114));

    // --- Brightness-adaptive scanlines ---
    // Bright pixels: scanlines are softer (beam blooms wider)
    // Dark pixels: scanlines are sharper (thin beam)
    float scanline_pos = TEX0.y * InputSize.y;
    float frac_y = fract(scanline_pos);

    float dist = abs(frac_y - 0.5) * 2.0;

    // Reduce scanline weight for bright pixels — simulates beam spread
    // luma=1 -> effective weight is halved, luma=0 -> full weight
    float adaptive_weight = SCANLINE_WEIGHT * (1.0 - luma * 0.5);
    float scanline = 1.0 - dist * dist * adaptive_weight;
    scanline = max(scanline, SCANLINE_GAP);

    color *= scanline;

    // --- RGB phosphor mask ---
    // Skip mask on very dark pixels (saves ALU, invisible anyway)
    float pixel_x = floor(TEX0.x * OutputSize.x);
    float phase = mod(pixel_x, 3.0);

    float is0 = 1.0 - step(0.5, phase);
    float is1 = step(0.5, phase) * (1.0 - step(1.5, phase));
    float is2 = step(1.5, phase);

    // Mask fades out on dark pixels: no visible effect, saves contrast
    float mask_fade = smoothstep(0.05, 0.25, luma);
    float effective_mask = MASK_STRENGTH * mask_fade;
    float dim = 1.0 - effective_mask;

    vec3 mask;
    mask.r = is0 + (1.0 - is0) * dim;
    mask.g = is1 + (1.0 - is1) * dim;
    mask.b = is2 + (1.0 - is2) * dim;

    color *= mask;

    // --- Color warmth (CRT phosphor tint) ---
    // Slight warm shift: boost red/green, cut blue
    color.r *= 1.0 + WARMTH;
    color.g *= 1.0 + WARMTH * 0.4;
    color.b *= 1.0 - WARMTH * 0.6;

    // --- Brightness compensation ---
    // Scanlines + mask darken the image; this brings it back up
    color *= BRIGHTNESS;

    // --- Vignette ---
    vec2 vig_uv = TEX0 * 2.0 - 1.0;
    float vig = 1.0 - dot(vig_uv, vig_uv) * VIGNETTE;
    color *= vig;

    // Clamp to prevent overbright from boost + glow
    color = min(color, vec3(1.0));

    gl_FragColor = vec4(color, 1.0);
}

#endif
