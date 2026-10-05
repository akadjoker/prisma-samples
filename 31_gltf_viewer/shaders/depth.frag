#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 7, std140) uniform Material
{
    vec4 uBaseColor;
    vec4 uSurface;
    vec4 uSpecular;
    vec4 uEmissive;
    vec4 uFlags;
    vec4 uModes;
};

layout(set = 1, binding = 2) uniform sampler2D uBaseTexture;

void main()
{
    float alpha = uBaseColor.a;
    if (uFlags.x > 0.5)
        alpha *= texture(uBaseTexture, vUv).a;
    if (alpha < uSurface.w)
        discard;
}
