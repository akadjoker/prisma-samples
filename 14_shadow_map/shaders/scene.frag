#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec4 vLightClip;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uLightViewProjection;
    vec4 uLightDirection;
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
layout(set = 1, binding = 1) uniform sampler2DShadow uShadow;

layout(location = 0) out vec4 oColor;

float shadowFactor(vec3 ndc)
{
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z - uParams.x;
    vec2 texel = 1.0 / vec2(textureSize(uShadow, 0));
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadow, vec3(uv + vec2(float(x), float(y)) * texel, depth));
    bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) &&
                  ndc.z <= 1.0;
    return inside ? sum / 9.0 : 1.0;
}

void main()
{
    vec3 normal = normalize(vNormal);
    float lambert = max(dot(normal, uLightDirection.xyz), 0.0);
    float shadow = shadowFactor(vLightClip.xyz / vLightClip.w);
    float ambient = mix(0.18, 0.32, normal.y * 0.5 + 0.5);
    vec3 albedo = texture(uDiffuse, vUv).rgb;
    vec3 color = albedo * (ambient + 0.9 * lambert * shadow);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
