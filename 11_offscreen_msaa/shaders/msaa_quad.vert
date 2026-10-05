#version 450

layout(location = 0) in vec2 aPosition;

layout(set = 0, binding = 0, std140) uniform Object
{
    mat4 uModelViewProjection;
};

layout(location = 0) out vec2 vUv;

void main()
{
    vUv = aPosition * 0.5 + 0.5;
    gl_Position = uModelViewProjection * vec4(aPosition, 0.0, 1.0);
}
