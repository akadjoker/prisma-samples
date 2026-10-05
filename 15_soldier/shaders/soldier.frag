#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vLight;

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;

layout(location = 0) out vec4 oColor;

void main()
{
    float diffuse = max(dot(normalize(vNormal), normalize(vLight)), 0.0);
    vec3 color = texture(uDiffuse, vUv).rgb * (0.25 + 0.85 * diffuse);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
