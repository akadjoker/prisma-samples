#version 450

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aPlace;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = aColor;
    gl_Position = vec4(aPosition * aPlace.z + aPlace.xy, 0.5, 1.0);
}
