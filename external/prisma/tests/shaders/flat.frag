#version 450

layout(set = 0, binding = 2, std140) uniform Params
{
    vec4 uColor;
    vec4 uPlace;
};

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = uColor;
}
