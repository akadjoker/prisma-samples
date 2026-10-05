#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

layout(set = 0, binding = 0, std140) uniform Object
{
    mat4 uModelViewProjection;
};

layout(location = 0) out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
