#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    mat4 uNormalMatrix;
};

layout(location = 0) out vec2 vUv;

invariant gl_Position;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
