#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 oColor;

void main()
{
    float falloff = max(1.0 - dot(vUv, vUv), 0.0);
    oColor = vec4(pow(vColor, vec3(1.0 / 2.2)), clamp(falloff * 2.0, 0.0, 1.0));
}
