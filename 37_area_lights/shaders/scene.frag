#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec2 vUv;

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
#include "../../common/shaders/ltc.glsl"
#include "../../common/shaders/tonemap.glsl"

const int kMaxAreaLights = 4;

layout(set = 0, binding = 3, std140) uniform AreaLights
{
    vec4 uCount;
    AreaLight uAreaLights[kMaxAreaLights];
};

layout(set = 1, binding = 3) uniform sampler2D uAlbedo;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCamera.xyz - vWorld);
    vec3 albedo = texture(uAlbedo, vUv).rgb * uTint.rgb;
    PbrSurface surface = makeSurface(albedo, uMaterial.y, uMaterial.x, n, v);

    vec3 color = evaluateIbl(surface, n, v) * uExposure.z;
    int count = int(uCount.x);
    for (int i = 0; i < kMaxAreaLights; ++i)
        if (i < count)
            color += areaLightShading(surface, uAreaLights[i], n, v, vWorld);

    color = tonemapFilmic(color * uExposure.x);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
