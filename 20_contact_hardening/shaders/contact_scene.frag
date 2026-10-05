#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uLightViewProjection;
    vec4 uLightDirection;
    vec4 uParams;
    vec4 uExtra;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
layout(set = 1, binding = 1) uniform sampler2DShadow uShadowCompare;
layout(set = 1, binding = 2) uniform sampler2D uShadowDepth;

layout(location = 0) out vec4 oColor;

const int kSearchSamples = 16;
const int kFilterSamples = 25;

float hash(vec2 position)
{
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(position, magic.xy)));
}

vec2 diskSample(int index, int count, float rotation)
{
    float radius = sqrt(float(index) + 0.5) / sqrt(float(count));
    float angle = float(index) * 2.4 + rotation;
    return vec2(cos(angle), sin(angle)) * radius;
}

float shadowFactor(vec3 normal, float facing)
{
    vec4 clip = uLightViewProjection * vec4(vWorld + normal * uExtra.x * (1.0 + 2.0 * (1.0 - facing)), 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) &&
                  ndc.z <= 1.0;
    if (!inside) return 1.0;

    float lightTan = uParams.y;
    float uvPerDepth = uParams.z;
    float slope = min(sqrt(max(1.0 - facing * facing, 0.0)) / max(facing, 0.2), 4.0);
    float rotation = hash(gl_FragCoord.xy) * 6.2831853;

    float searchRadius = min(ndc.z * lightTan * uvPerDepth, uParams.w);
    float searchDepth = ndc.z - uParams.x - uExtra.y * slope / uvPerDepth;
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (int i = 0; i < kSearchSamples; ++i)
    {
        vec2 offset = diskSample(i, kSearchSamples, rotation) * searchRadius;
        float blocker = texture(uShadowDepth, uv + offset).r;
        if (blocker < searchDepth)
        {
            blockerSum += blocker;
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5) return 1.0;

    float blockerDepth = blockerSum / blockerCount;
    float penumbra = (ndc.z - blockerDepth) * lightTan * uvPerDepth;
    float radius = clamp(penumbra, uExtra.y, uParams.w);
    float depth = ndc.z - uParams.x - radius * slope / uvPerDepth;

    float lit = 0.0;
    for (int i = 0; i < kFilterSamples; ++i)
    {
        vec2 offset = diskSample(i, kFilterSamples, rotation) * radius;
        lit += texture(uShadowCompare, vec3(uv + offset, depth));
    }
    return lit / float(kFilterSamples);
}

void main()
{
    vec3 normal = normalize(vNormal);
    float facing = max(dot(normal, uLightDirection.xyz), 0.0);
    float shadow = shadowFactor(normal, facing);
    float ambient = mix(0.18, 0.32, normal.y * 0.5 + 0.5);
    vec3 albedo = texture(uDiffuse, vUv).rgb;
    vec3 color = albedo * (ambient + 0.9 * facing * shadow);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
