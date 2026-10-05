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

#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = tonemapFilmic(uTint.rgb * uMaterial.z * uExposure.x);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
