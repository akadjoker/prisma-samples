#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uModel;
    vec4 uCamera;
    vec4 uLightDirection;
    vec4 uLevels;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 light = normalize(uLightDirection.xyz);
    vec3 view = normalize(uCamera.xyz - vWorld);
    float diffuse = max(dot(normal, light), 0.0);
    float specular = pow(max(dot(normal, normalize(light + view)), 0.0), 48.0);
    float ambient = 0.25 + 0.15 * normal.y;
    vec3 albedo = texture(uDiffuse, vUv).rgb;
    vec3 color = albedo * (ambient + 0.85 * diffuse) + vec3(0.25) * specular;
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
