#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uLightViewProjection;
    vec4 uLightDirection;
    vec4 uParams;
    vec4 uExtra;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vWorld;

void main()
{
    vNormal = aNormal;
    vUv = aUv;
    vWorld = aPosition;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
