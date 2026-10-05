#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBloom;

#include "tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uScene, vUv).rgb;
    if (uParams.x > 0.0) color += texture(uBloom, vUv).rgb * uParams.x;
    vec3 encoded = linearToSrgb(tonemapAcesLegacy(color));
    oColor = vec4(encoded, dot(encoded, vec3(0.2126, 0.7152, 0.0722)));
}
