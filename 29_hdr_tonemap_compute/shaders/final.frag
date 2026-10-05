#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uScene;
layout(set = 1, binding = 1) uniform sampler2D uBloom;

layout(set = 2, binding = 0, std430) readonly buffer Luminance
{
    float uLuminance[];
};

layout(location = 0) out vec4 oColor;

const float kMiddleGray = 0.18;
const float kWhite = 1.5;

void main()
{
    vec3 color = texture(uScene, vUv).rgb;
    vec3 bloom = texture(uBloom, vUv).rgb;

    color *= kMiddleGray / (uLuminance[0] + 0.001);
    color *= 1.0 + color / kWhite;
    color /= 1.0 + color;
    color += uParams.x * bloom;

    oColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
