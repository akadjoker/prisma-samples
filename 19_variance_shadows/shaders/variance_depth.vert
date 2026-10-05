#version 450

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0, std140) uniform Light
{
    mat4 uLightViewProjection;
};

void main()
{
    gl_Position = uLightViewProjection * vec4(aPosition, 1.0);
}
