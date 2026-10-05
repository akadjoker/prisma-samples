#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    mat4 uModel;
    vec4 uCameraPosition;
    vec4 uParams;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vWorld = world.xyz;
    gl_Position = uViewProjection * world;
}
