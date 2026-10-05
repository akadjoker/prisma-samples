#version 450

layout(set = 0, binding = 0, std140) uniform Ground
{
    mat4 uViewProjection;
    vec4 uColor;
};

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = uColor;
}
