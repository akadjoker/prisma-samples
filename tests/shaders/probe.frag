#version 450

layout(set = 0, binding = 0, std140) uniform Probe
{
    vec4 uDirection;
    vec4 uLevel;
};

layout(set = 1, binding = 0) uniform samplerCube uCube;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(textureLod(uCube, uDirection.xyz, uLevel.x).rgb * 0.5, 1.0);
}
