#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vRay;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 1, binding = 0) uniform samplerCube uSky;

#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = textureLod(uSky, normalize(vRay), uExposure.y).rgb * uExposure.x * uExposure.w;
    color = tonemapFilmic(color);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
