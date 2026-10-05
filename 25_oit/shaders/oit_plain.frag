#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uScreen;
    vec4 uLight;
};

layout(set = 0, binding = 1, std140) uniform Object
{
    mat4 uModel;
    vec4 uColor;
};

layout(location = 0) out vec4 oColor;

void main()
{
    float facing = abs(dot(normalize(vNormal), normalize(uLight.xyz)));
    oColor = vec4(uColor.rgb * (0.55 + 0.45 * facing), uColor.a);
}
