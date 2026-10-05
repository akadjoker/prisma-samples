#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(set = 0, binding = 0, std140) uniform Screen
{
    vec4 uScreen;
};

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = vec4(aPosition.x * uScreen.x - 1.0, 1.0 - aPosition.y * uScreen.y, 0.0, 1.0);
}
