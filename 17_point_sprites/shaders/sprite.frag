#version 450

layout(location = 0) in vec2 gUv;
layout(location = 1) in vec4 gColor;

layout(location = 0) out vec4 oColor;

void main()
{
    float falloff = 1.0 - clamp(length(gUv), 0.0, 1.0);
    falloff *= falloff;
    oColor = vec4(gColor.rgb * falloff * 0.12, 1.0);
}
