#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uCamera;
    vec4 uLightDirection;
    vec4 uParams;
    vec4 uFog;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
layout(set = 1, binding = 1) uniform sampler2D uNormalHeight;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 tangentNormal = texture(uNormalHeight, vUv).rgb * 2.0 - 1.0;
    vec3 normal = normalize(vec3(tangentNormal.x, tangentNormal.z, tangentNormal.y));
    vec3 light = normalize(uLightDirection.xyz);
    vec3 toCamera = uCamera.xyz - vWorld;
    float distanceToCamera = length(toCamera);
    vec3 view = toCamera / distanceToCamera;

    float diffuse = max(dot(normal, light), 0.0);
    float specular = pow(max(dot(normal, normalize(light + view)), 0.0), 24.0);
    float ambient = 0.3 + 0.2 * normal.y;
    vec3 albedo = texture(uDiffuse, vUv).rgb;
    vec3 color = albedo * (ambient + 0.9 * diffuse) + vec3(0.12) * specular;

    float fog = 1.0 - exp(-distanceToCamera * uFog.w);
    oColor = vec4(mix(pow(color, vec3(1.0 / 2.2)), uFog.rgb, fog), 1.0);
}
