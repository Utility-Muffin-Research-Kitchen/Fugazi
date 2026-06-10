// Fugazi CRT - Pass 1: Barrel distortion + soft glow
// Optimized for Mali-G31 (TrimUI Brick)

#if defined(VERTEX)

uniform mediump mat4 MVPMatrix;
attribute mediump vec4 VertexCoord;
attribute mediump vec2 TexCoord;

varying mediump vec2 TEX0;

void main()
{
    // No Y-flip: match RetroArch's stock convention. RA pads the frame texture,
    // so the content lives in the top InputSize/TextureSize fraction; sampling
    // TexCoord directly hits it. Flipping here sampled the black padding region
    // (that was the "over half black" bug).
    TEX0 = TexCoord;
    gl_Position = MVPMatrix * VertexCoord;
}

#elif defined(FRAGMENT)


precision mediump float;
precision mediump int;
uniform sampler2D Texture;
uniform vec2 InputSize;
uniform vec2 TextureSize;

varying vec2 TEX0;

#define CURVATURE 0.06
#define GLOW_MIX 0.35

void main()
{
    // RA pads the frame texture: valid content occupies [0, scale] of TEX0,
    // where scale = InputSize/TextureSize. Normalize to [0,1] content space,
    // do the geometry there, then convert back to texcoord space to sample.
    vec2 scale = InputSize / TextureSize;
    vec2 uv01 = TEX0 / scale;

    // --- Barrel distortion (quadratic, no trig) ---
    vec2 centered = uv01 - vec2(0.5);
    float r2 = dot(centered, centered);
    centered *= 1.0 + CURVATURE * r2;
    uv01 = centered + vec2(0.5);

    // Clip to the (curved) screen edges, in normalized space
    vec2 edge = step(vec2(0.0), uv01) * step(uv01, vec2(1.0));
    float inside = edge.x * edge.y;

    // Back to texcoord space for sampling
    vec2 uv = uv01 * scale;

    // --- Soft glow via 5-tap cross blur ---
    vec2 offset_x = vec2(2.0 / TextureSize.x, 0.0);
    vec2 offset_y = vec2(0.0, 2.0 / TextureSize.y);

    vec3 center = texture2D(Texture, uv).rgb;
    vec3 blur  = texture2D(Texture, uv + offset_x).rgb;
         blur += texture2D(Texture, uv - offset_x).rgb;
         blur += texture2D(Texture, uv + offset_y).rgb;
         blur += texture2D(Texture, uv - offset_y).rgb;
    blur *= 0.25;

    // Mix sharp center with soft blur for glow
    vec3 color = mix(center, blur, GLOW_MIX);

    gl_FragColor = vec4(color * inside, 1.0);
}

#endif
