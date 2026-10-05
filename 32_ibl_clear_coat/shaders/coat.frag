#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
    vec4 uBaseColor;
    vec4 uMaterial;
    mat4 uModel;
};

#include "../../common/shaders/pbr.glsl"
#include "../../common/shaders/lighting.glsl"
#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCamera.xyz - vWorld);
    PbrSurface surface = makeClearCoatSurface(uBaseColor.rgb, uMaterial.x, uMaterial.y,
            uMaterial.z, uMaterial.w, n, v);
    vec3 color = evaluateIbl(surface, n, v) +
                 surfaceShading(surface, directionalLight(uSunDirection, uSunColorIntensity), n, v);
    oColor = vec4(linearToSrgb(tonemapAcesLegacy(color)), 1.0);
}
