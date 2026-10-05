#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBloom;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uScene, vUv).rgb + texture(uBloom, vUv).rgb * uParams.y;
    color *= uParams.x;
    color = color * (2.51 * color + 0.03) / (color * (2.43 * color + 0.59) + 0.14);
    oColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
