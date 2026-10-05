#version 450

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vColor;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uSize;
    vec4 uOptions;
};

layout(set = 1, binding = 0) uniform sampler2D uSprite;

layout(location = 0) out vec4 oColor;

void main()
{
    float glow;
    if (uOptions.z > 0.5)
    {
        glow = texture(uSprite, vUv * 0.5 + 0.5).a;
    }
    else
    {
        float falloff = max(1.0 - dot(vUv, vUv), 0.0);
        glow = falloff * falloff;
    }
    oColor = vec4(vColor * glow, 1.0);
}
