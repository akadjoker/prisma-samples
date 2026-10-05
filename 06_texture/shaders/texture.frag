#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uModelViewProjection;
    vec4 uTint;
};

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uTexture, vUv).rgb * uTint.rgb;
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
