#version 450

layout(location = 0) in vec2 aPosition;

layout(set = 0, binding = 2, std140) uniform Params
{
    vec4 uColor;
    vec4 uPlace;
};

void main()
{
    gl_Position = vec4(aPosition, uPlace.z, 1.0);
}
