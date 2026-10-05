#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    vec4 uTint;
    vec4 uMaterial;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out vec2 vUv;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
