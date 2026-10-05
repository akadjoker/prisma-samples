#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
};

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    mat4 uNormalMatrix;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out vec3 vClip;
layout(location = 3) out vec4 vTangent;
layout(location = 4) out vec2 vUv;

invariant gl_Position;

void main()
{
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = mat3(uNormalMatrix) * aNormal;
    vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
    vUv = aUv;
    gl_Position = uViewProjection * world;
    vClip = gl_Position.xyw;
}
