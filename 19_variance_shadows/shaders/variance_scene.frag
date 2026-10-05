#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in float vViewDepth;
layout(location = 3) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uView;
    mat4 uLightViewProjection;
    vec4 uLightDirection;
    vec4 uParams;
    vec4 uFog;
};

layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
layout(set = 1, binding = 1) uniform sampler2D uMoments;

layout(location = 0) out vec4 oColor;

const vec3 kSky = vec3(0.55, 0.68, 0.85);

float varianceShadow(vec3 normal, float facing)
{
    float offset = uParams.x * (1.0 + 2.0 * (1.0 - facing));
    vec4 clip = uLightViewProjection * vec4(vWorld + normal * offset, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) &&
                  ndc.z <= 1.0;
    if (!inside) return 1.0;

    vec2 moments = texture(uMoments, uv).xy;
    float depth = ndc.z;
    if (depth <= moments.x) return 1.0;
    float variance = max(moments.y - moments.x * moments.x, uParams.z);
    float delta = depth - moments.x;
    float p = variance / (variance + delta * delta);
    return smoothstep(uParams.y, 1.0, p);
}

void main()
{
    vec3 normal = normalize(vNormal);
    float facing = max(dot(normal, uLightDirection.xyz), 0.0);
    float shadow = varianceShadow(normal, facing);

    vec3 albedo = texture(uDiffuse, vUv).rgb;
    if (uFog.z > 0.5)
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
    color = pow(color, vec3(1.0 / 2.2));
    float fog = smoothstep(uFog.x, uFog.y, vViewDepth);
    oColor = vec4(mix(color, kSky, fog), 1.0);
}
