#ifndef SUN_SHADOW_GLSL
#define SUN_SHADOW_GLSL

layout(set = 0, binding = 8, std140) uniform SunShadow
{
    mat4 uSunShadowMatrix;
    vec4 uSunShadowParams;
    vec4 uSunShadowAxisX;
    vec4 uSunShadowAxisY;
};

layout(set = 1, binding = 7) uniform sampler2DShadow uSunShadowMap;

float sunShadow(vec3 worldPosition, vec3 normal)
{
    float size = uSunShadowParams.x;
    float texelSize = uSunShadowParams.y;
    vec2 lateral = vec2(dot(normal, uSunShadowAxisX.xyz), dot(normal, uSunShadowAxisY.xyz));
    vec3 p = worldPosition + normal * (abs(lateral.x * uSunShadowParams.z) +
                                        abs(lateral.y * uSunShadowParams.w));
    vec4 clip = uSunShadowMatrix * vec4(p, 1.0);
    vec3 position = clip.xyz / clip.w;
    position.xy = position.xy * 0.5 + 0.5;
    position.z = clamp(position.z, 0.0, 1.0);
    position.xy = clamp(position.xy, vec2(-1.0), vec2(2.0));

    vec2 offset = vec2(0.5);
    vec2 uv = position.xy * size + offset;
    vec2 base = (floor(uv) - offset) * texelSize;
    vec2 st = fract(uv);
    vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
    vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);
    vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * texelSize;
    vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * texelSize;

    float sum = uw.x * vw.x * texture(uSunShadowMap, vec3(base + vec2(u.x, v.x), position.z));
    sum += uw.y * vw.x * texture(uSunShadowMap, vec3(base + vec2(u.y, v.x), position.z));
    sum += uw.x * vw.y * texture(uSunShadowMap, vec3(base + vec2(u.x, v.y), position.z));
    sum += uw.y * vw.y * texture(uSunShadowMap, vec3(base + vec2(u.y, v.y), position.z));
    return sum * (1.0 / 16.0);
}

#endif
