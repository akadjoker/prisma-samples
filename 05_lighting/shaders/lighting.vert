#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform Scene
{
    mat4 uModelViewProjection;
    mat4 uModel;
    vec4 uLightDirection[2];
    vec4 uLightColor[2];
    vec4 uAmbient;
};

layout(location = 0) out vec3 vNormal;

void main()
{
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
