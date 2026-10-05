#version 450

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(textureLod(uSource, vec2(0.5, 0.5), 0.0).rgb * 0.25, 1.0);
}
