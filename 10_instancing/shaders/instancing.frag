#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 light = normalize(vec3(0.4, 0.8, 0.5));
    float diffuse = max(dot(normalize(vNormal), light), 0.0);
    vec3 color = vColor * (0.25 + 0.85 * diffuse);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
