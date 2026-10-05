#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uView;
    mat4 uLightViewProjection[4];
    vec4 uLightDirection;
    vec4 uSplits;
    vec4 uTexels;
    vec4 uParams;
    vec4 uExtra;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out float vViewDepth;
layout(location = 3) out vec2 vUv;

void main()
{
    vNormal = aNormal;
    vWorld = aPosition;
    vUv = aUv;
    vViewDepth = -(uView * vec4(aPosition, 1.0)).z;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
