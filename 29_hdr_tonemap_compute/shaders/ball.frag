#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    mat4 uModel;
    vec4 uCameraPosition;
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform samplerCube uEnvironment;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 view = normalize(uCameraPosition.xyz - vWorld);
    vec3 reflected = reflect(-view, normal);
    float fresnel = pow(1.0 - max(dot(normal, view), 0.0), 5.0);
    vec3 environment = texture(uEnvironment, reflected).rgb;
    oColor = vec4(environment * mix(0.6, 1.0, fresnel) * uParams.x, 1.0);
}
