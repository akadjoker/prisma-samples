#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 6) in vec4 aWeights;
layout(location = 7) in uvec4 aIndices;

layout(set = 0, binding = 0, std140) uniform Skin
{
    mat4 uViewProjection;
    mat4 uModel;
    vec4 uLightDirection;
    mat4 uBones[128];
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vLight;

void main()
{
    mat4 skin = aWeights.x * uBones[aIndices.x] + aWeights.y * uBones[aIndices.y] +
                aWeights.z * uBones[aIndices.z] + aWeights.w * uBones[aIndices.w];
    vec4 position = skin * vec4(aPosition, 1.0);
    vec3 normal = mat3(skin) * aNormal;
    vNormal = mat3(uModel) * normal;
    vUv = aUv;
    vLight = uLightDirection.xyz;
    gl_Position = uViewProjection * (uModel * position);
}
