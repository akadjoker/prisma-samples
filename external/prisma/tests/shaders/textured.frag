#version 450

layout(location = 0) in vec2 vUv;

layout(set = 1, binding = 3) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = texture(uTexture, vUv);
}
