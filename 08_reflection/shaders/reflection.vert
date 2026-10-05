#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uModelViewProjection;
    mat4 uModel;
    vec4 uCameraPosition;
    vec4 uLightDirection;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out vec2 vUv;

void main()
{
    vNormal = mat3(uModel) * aNormal;
    vWorld = (uModel * vec4(aPosition, 1.0)).xyz;
    vUv = aUv;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
