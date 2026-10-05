#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in float vViewDepth;
layout(location = 3) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uView;
    mat4 uLightViewProjection[4];
    vec4 uLightDirection;
    vec4 uSplits;
    vec4 uTexels;
    vec4 uParams;
    vec4 uExtra;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
layout(set = 1, binding = 1) uniform sampler2DArrayShadow uShadow;

layout(location = 0) out vec4 oColor;

const vec3 kSky = vec3(0.55, 0.68, 0.85);
const vec3 kTint[4] = vec3[](vec3(1.0, 0.35, 0.35), vec3(0.35, 1.0, 0.35), vec3(0.4, 0.5, 1.0),
        vec3(1.0, 0.9, 0.3));

float cascadeShadow(int cascade, vec3 normal, float facing)
{
    float offset = uTexels[cascade] * uParams.y * (1.0 + 2.0 * (1.0 - facing));
    vec4 clip = uLightViewProjection[cascade] * vec4(vWorld + normal * offset, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float texel = 1.0 / float(textureSize(uShadow, 0).x);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += texture(uShadow,
                    vec4(uv + vec2(float(x), float(y)) * texel, float(cascade), ndc.z));
    return sum / 9.0;
}

void main()
{
    vec3 normal = normalize(vNormal);
    float facing = max(dot(normal, uLightDirection.xyz), 0.0);

    int cascade = 0;
    for (int i = 0; i < 3; ++i)
        if (vViewDepth > uSplits[i]) cascade = i + 1;

    float shadow = cascadeShadow(cascade, normal, facing);
    if (cascade < 3)
    {
        float start = cascade == 0 ? uExtra.y : uSplits[cascade - 1];
        float t = (uSplits[cascade] - vViewDepth) / (uSplits[cascade] - start);
        if (t < 0.1) shadow = mix(cascadeShadow(cascade + 1, normal, facing), shadow, t / 0.1);
    }

    vec3 albedo = texture(uDiffuse, vUv).rgb;
    if (uExtra.x > 0.5)
    {
        albedo = vec3(0.82, 0.80, 0.76);
        if (vWorld.y < 0.02 && normal.y > 0.99)
        {
            float check = mod(floor(vWorld.x / 8.0) + floor(vWorld.z / 8.0), 2.0);
            albedo = vec3(0.50, 0.58, 0.42) * mix(0.8, 1.0, check);
        }
    }

    float ambient = mix(0.20, 0.34, normal.y * 0.5 + 0.5);
    vec3 color = albedo * (ambient + 0.9 * facing * shadow);
    if (uParams.x > 0.5) color *= mix(vec3(1.0), kTint[cascade], 0.55);
    color = pow(color, vec3(1.0 / 2.2));
    float fog = smoothstep(uParams.z, uParams.w, vViewDepth);
    oColor = vec4(mix(color, kSky, fog), 1.0);
}
