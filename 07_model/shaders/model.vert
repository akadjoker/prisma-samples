#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uModelViewProjection;
    mat4 uModel;
    vec4 uLightDirection;
    vec4 uParams;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;

void main()
{
    vec3 position = aPosition + aNormal * uParams.x;
    vNormal = mat3(uModel) * aNormal;
    vUv = aUv;
    gl_Position = uModelViewProjection * vec4(position, 1.0);
}
