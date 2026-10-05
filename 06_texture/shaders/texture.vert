#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uModelViewProjection;
    vec4 uTint;
};

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = aUv;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
