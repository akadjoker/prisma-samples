#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
    vec4 uBaseColor;
    vec4 uMaterial;
    mat4 uModel;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProjection * world;
}
