#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uModelViewProjection;
    mat4 uModel;
    vec4 uCameraPosition;
    vec4 uLightDirection;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
layout(set = 1, binding = 1) uniform samplerCube uEnvironment;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 view = normalize(uCameraPosition.xyz - vWorld);
    vec3 reflected = reflect(-view, normal);
    float diffuse = max(dot(normal, uLightDirection.xyz), 0.0);
    vec3 surface = texture(uDiffuse, vUv).rgb * (0.25 + 0.85 * diffuse);
    vec3 environment = texture(uEnvironment, reflected).rgb;
    oColor = vec4(pow(mix(surface, environment, 0.5), vec3(1.0 / 2.2)), 1.0);
}
