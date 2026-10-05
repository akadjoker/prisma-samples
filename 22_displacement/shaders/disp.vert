#version 450

layout(location = 0) in vec2 aPosition;

void main()
{
    gl_Position = vec4(aPosition.x, 0.0, aPosition.y, 1.0);
}
