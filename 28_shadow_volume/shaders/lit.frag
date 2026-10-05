#version 450

layout(location = 0) in vec3 vViewPos;
layout(location = 1) in vec3 vViewNormal;
layout(location = 2) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 uMvp;
    mat4 uMv;
    mat4 uProj;
    vec4 uLightView;
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 toLight = uLightView.xyz - vViewPos;
    float lengthSquared = dot(toLight, toLight);
    float diffuse = clamp(dot(normalize(vViewNormal), normalize(toLight)), 0.0, 1.0) *
                    uParams.y / lengthSquared;
    vec3 color = texture(uDiffuse, vUv).rgb * (uParams.x + diffuse);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
