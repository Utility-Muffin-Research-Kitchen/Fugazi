// Fugazi CRT - Pass 1: Barrel distortion + soft glow
// Optimized for Mali-G31 (TrimUI Brick)

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

varying vec2 TEX0;

#define CURVATURE 0.06
#define GLOW_MIX 0.35

void main()
{
    // --- Barrel distortion (quadratic, no trig) ---
    vec2 scale = TextureSize / InputSize;
    vec2 tex0 = TEX0 * scale;
    vec2 centered = tex0 - vec2(0.5);
    float r2 = dot(centered, centered);
    centered *= 1.0 + CURVATURE * r2;

    // Clip outside curved screen
    vec2 edge = step(vec2(-0.5), centered) * step(centered, vec2(0.5));
    float inside = edge.x * edge.y;

    vec2 uv = (centered + vec2(0.5)) / scale;

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
