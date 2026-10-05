#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec3 vClip;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    vec4 uBaseColor;
    vec4 uMaterial;
};

#include "../../common/shaders/pbr.glsl"
#include "../../common/shaders/lights_clustered.glsl"
#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCamera.xyz - vWorld);
    PbrSurface surface = makeSurface(uBaseColor.rgb, uMaterial.x, uMaterial.y, n, v);
    vec3 color = evaluateIbl(surface, n, v) * uExposure.z + evaluateLights(surface, n, v, vWorld, vClip);
    if (uMaterial.z > 0.0)
        color = uBaseColor.rgb * uMaterial.z;
    color = tonemapFilmic(color * uExposure.x);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
