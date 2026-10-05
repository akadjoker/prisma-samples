#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Params
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uColor;

#include "fxaa.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 texelSize = vec2(1.0) / vec2(textureSize(uColor, 0));
    vec4 color = textureLod(uColor, vUv, 0.0);
    if (uParams.x > 0.5)
    {
        color = fxaa(vUv, vec4(vUv - texelSize * 0.5, vUv + texelSize * 0.5), texelSize,
                2.0 * texelSize, 8.0, 0.08, 0.04);
    }
    oColor = vec4(color.rgb, 1.0);
}
