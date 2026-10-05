#version 450

layout(location = 0) in vec2 vUv;

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

vec3 box4x4(vec3 s0, vec3 s1, vec3 s2, vec3 s3)
{
    return (s0 + s1 + s2 + s3) * 0.25;
}

void main()
{
    vec2 uv = vUv;
    vec3 c = textureLod(uSource, uv, 0.0).rgb;

    vec3 lt = textureLodOffset(uSource, uv, 0.0, ivec2(-1, -1)).rgb;
    vec3 rt = textureLodOffset(uSource, uv, 0.0, ivec2(1, -1)).rgb;
    vec3 rb = textureLodOffset(uSource, uv, 0.0, ivec2(1, 1)).rgb;
    vec3 lb = textureLodOffset(uSource, uv, 0.0, ivec2(-1, 1)).rgb;

    vec3 lt2 = textureLodOffset(uSource, uv, 0.0, ivec2(-2, -2)).rgb;
    vec3 rt2 = textureLodOffset(uSource, uv, 0.0, ivec2(2, -2)).rgb;
    vec3 rb2 = textureLodOffset(uSource, uv, 0.0, ivec2(2, 2)).rgb;
    vec3 lb2 = textureLodOffset(uSource, uv, 0.0, ivec2(-2, 2)).rgb;

    vec3 l = textureLodOffset(uSource, uv, 0.0, ivec2(-2, 0)).rgb;
    vec3 t = textureLodOffset(uSource, uv, 0.0, ivec2(0, -2)).rgb;
    vec3 r = textureLodOffset(uSource, uv, 0.0, ivec2(2, 0)).rgb;
    vec3 b = textureLodOffset(uSource, uv, 0.0, ivec2(0, 2)).rgb;

    vec3 c0 = box4x4(lt, rt, rb, lb);
    vec3 c1 = box4x4(c, l, t, lt2);
    c1 += box4x4(c, r, t, rt2);
    c1 += box4x4(c, r, b, rb2);
    c1 += box4x4(c, l, b, lb2);

    oColor = vec4(c0 * 0.5 + c1 * 0.125, 1.0);
}
