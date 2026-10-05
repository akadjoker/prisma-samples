#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 uMvp;
    mat4 uMv;
    mat4 uProj;
    vec4 uLightView;
    vec4 uParams;
};

layout(location = 0) out vec3 vViewPos;
layout(location = 1) out vec3 vViewNormal;
layout(location = 2) out vec2 vUv;

invariant gl_Position;

void main()
{
    vViewPos = (uMv * vec4(aPosition, 1.0)).xyz;
    vViewNormal = mat3(uMv) * aNormal;
    vUv = aUv;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
