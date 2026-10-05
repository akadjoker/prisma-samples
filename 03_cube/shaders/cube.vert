#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uModelViewProjection;
};

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUv;

void main()
{
    vColor = aColor;
    vUv = aUv;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
