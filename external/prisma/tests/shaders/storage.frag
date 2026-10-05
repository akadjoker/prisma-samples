#version 450

layout(set = 2, binding = 1, std430) readonly buffer Colors
{
    vec4 uColors[2];
};

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = uColors[1];
}
