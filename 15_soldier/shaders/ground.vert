#version 450

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0, std140) uniform Ground
{
    mat4 uViewProjection;
    vec4 uColor;
};

void main()
{
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
