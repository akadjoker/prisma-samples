#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in float vHeight;

layout(set = 0, binding = 0, std140) uniform Surface
{
    mat4 uViewProjection;
    vec4 uLevel;
    vec4 uLightDirection;
};

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 base = mix(vec3(0.10, 0.25, 0.60), vec3(0.85, 0.90, 0.95), clamp(vHeight * 0.6 + 0.5, 0.0, 1.0));
    float diffuse = max(dot(normalize(vNormal), normalize(uLightDirection.xyz)), 0.0);
    vec3 color = base * (0.2 + 0.9 * diffuse);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
