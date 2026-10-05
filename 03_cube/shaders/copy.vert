#version 450

layout(location = 0) in vec2 aPosition;

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.5, 1.0);
}
