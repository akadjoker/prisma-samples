#version 450

layout(location = 0) in vec3 vViewPos;
layout(location = 1) in vec3 vViewNormal;
layout(location = 2) in vec2 vUv;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(1.0, 0.96, 0.82, 1.0);
}
