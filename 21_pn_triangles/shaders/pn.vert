#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;

void main()
{
    vNormal = aNormal;
    vUv = aUv;
    gl_Position = vec4(aPosition, 1.0);
}
