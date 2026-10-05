#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUv;

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(vColor * texture(uTexture, vUv).rgb, 1.0);
}
