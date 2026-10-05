#ifndef LIGHT_SHADOWS_GLSL
#define LIGHT_SHADOWS_GLSL

const int kMaxShadowMaps = 192;

layout(set = 0, binding = 8, std140) uniform LightShadows
{
    vec4 uShadowParams;
    mat4 uShadowMatrix[kMaxShadowMaps];
    vec4 uShadowInfo[kMaxShadowMaps];
};

layout(set = 1, binding = 7) uniform sampler2DArrayShadow uShadowMaps;

int pointShadowFace(vec3 r)
{
    vec3 a = abs(r);
    float d = max(a.x, max(a.y, a.z));
    if (d == a.x) return r.x >= 0.0 ? 0 : 1;
    if (d == a.y) return r.y >= 0.0 ? 2 : 3;
    return r.z >= 0.0 ? 4 : 5;
}

float lightShadow(vec4 positionFalloff, vec4 direction, vec4 spot, vec3 worldPosition,
        vec3 normal, vec3 l)
{
    if (spot.w < 0.5) return 1.0;
    int map = int(spot.w) - 1;
    bool point = spot.z < 0.5;
    vec3 r = worldPosition - positionFalloff.xyz;
    vec3 a = abs(r);
    float depth = point ? max(a.x, max(a.y, a.z)) : dot(r, direction.xyz);
    float texel = depth * uShadowInfo[map].x * uShadowParams.x;
    float facing = clamp(dot(normal, l), 0.0, 1.0);
    vec3 p = worldPosition + normal * (texel * uShadowParams.y * (1.0 + 2.0 * (1.0 - facing)));
    if (point) map += pointShadowFace(p - positionFalloff.xyz);
    vec4 clip = uShadowMatrix[map] * vec4(p, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    return texture(uShadowMaps, vec4(ndc.xy * 0.5 + 0.5, float(map), ndc.z));
}

#endif
