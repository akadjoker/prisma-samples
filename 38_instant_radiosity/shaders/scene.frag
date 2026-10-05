#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec3 vClip;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    vec4 uTint;
    vec4 uMaterial;
};

#include "../../common/shaders/pbr.glsl"
#include "../../common/shaders/lights_clustered.glsl"
#include "../../common/shaders/tonemap.glsl"

// The light that shines directly, a spot light with the shadow map of the reflective shadow map
// pass.
layout(set = 0, binding = 7, std140) uniform Direct
{
    LightData uDirect;
    mat4 uLightViewProjection;
    // x: depth bias, y: normal offset, z: scale of the indirect light, w: scale of the direct light
    vec4 uShadow;
};

layout(set = 1, binding = 3) uniform sampler2D uAlbedo;
layout(set = 1, binding = 4) uniform sampler2DShadow uShadowMap;

layout(location = 0) out vec4 oColor;

const float kMinDistanceSquared = 0.09;

float shadowFactor(vec3 world, vec3 n)
{
    vec4 clip = uLightViewProjection * vec4(world + n * uShadow.y, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z - uShadow.x;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadowMap, vec3(uv + vec2(float(x), float(y)) * texel, depth));
    bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) &&
                  ndc.z <= 1.0;
    return inside ? sum / 9.0 : 1.0;
}

void main()
{
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCamera.xyz - vWorld);
    vec3 albedo = texture(uAlbedo, vUv).rgb * uTint.rgb;
    PbrSurface surface = makeSurface(albedo, uMaterial.y, uMaterial.x, n, v);

    vec3 color = evaluateIbl(surface, n, v);

    Light direct = punctualLight(uDirect, vWorld);
    if (direct.attenuation > 0.0 && dot(n, direct.l) > 0.0)
        color += surfaceShading(surface, direct, n, v) * (shadowFactor(vWorld, n) * uShadow.w);

    // the virtual point lights of the froxel the point is in
    uint index = froxelIndex(vClip);
    uint entry = uFroxels[index >> 2u][index & 3u];
    uint first = entry >> 16u;
    uint count = entry & 0xFFu;
    vec3 indirect = vec3(0.0);
    for (uint i = 0u; i < count; ++i)
    {
        LightData data = uLights[lightIndexAt(first + i)];
        vec3 posToLight = data.positionFalloff.xyz - vWorld;
        Light light = punctualLight(data, vWorld);
        // a virtual light is no point: without a floor under the distance the surface next to
        // it would be a bright spot
        float distanceSquared = dot(posToLight, posToLight);
        light.attenuation *= distanceSquared / max(distanceSquared, kMinDistanceSquared);
        if (light.attenuation <= 0.0 || dot(n, light.l) <= 0.0)
            continue;
        indirect += surfaceShading(surface, light, n, v);
    }
    color += indirect * uShadow.z;

    if (uMaterial.z > 0.0)
        color = uTint.rgb * uMaterial.z;
    color = tonemapFilmic(color * uExposure.x);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
