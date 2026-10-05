#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
};

layout(set = 0, binding = 7, std140) uniform Refraction
{
    vec4 uBaseIor;
    vec4 uAbsorptionThickness;
    vec4 uParams;
};

layout(set = 1, binding = 2) uniform sampler2D uPyramid0;
layout(set = 1, binding = 3) uniform sampler2D uPyramid1;
layout(set = 1, binding = 4) uniform sampler2D uPyramid2;
layout(set = 1, binding = 5) uniform sampler2D uPyramid3;
layout(set = 1, binding = 6) uniform sampler2D uPyramid4;
layout(set = 1, binding = 7) uniform sampler2D uPyramid5;
layout(set = 1, binding = 8) uniform sampler2D uPyramid6;

#include "../../common/shaders/pbr.glsl"

layout(location = 0) out vec4 oColor;

vec3 pyramidLevel(int level, vec2 uv)
{
    if (level == 0) return textureLod(uPyramid0, uv, 0.0).rgb;
    if (level == 1) return textureLod(uPyramid1, uv, 0.0).rgb;
    if (level == 2) return textureLod(uPyramid2, uv, 0.0).rgb;
    if (level == 3) return textureLod(uPyramid3, uv, 0.0).rgb;
    if (level == 4) return textureLod(uPyramid4, uv, 0.0).rgb;
    if (level == 5) return textureLod(uPyramid5, uv, 0.0).rgb;
    return textureLod(uPyramid6, uv, 0.0).rgb;
}

vec3 samplePyramid(vec2 uv, float lod)
{
    float l = clamp(lod, 0.0, 6.0);
    int l0 = int(floor(l));
    int l1 = min(l0 + 1, 6);
    return mix(pyramidLevel(l0, uv), pyramidLevel(l1, uv), l - float(l0));
}

void main()
{
    vec3 n = normalize(vNormal);
    vec3 v = normalize(uCamera.xyz - vWorld);
    vec3 baseColor = uBaseIor.rgb;
    float ior = uBaseIor.w;
    float roughness = uParams.x;
    float metallic = uParams.y;
    float transmission = uParams.z;

    PbrSurface surface = makeSurface(baseColor, metallic, roughness, n, v);
    float f0 = (ior - 1.0) / (ior + 1.0);
    surface.f0 = mix(vec3(f0 * f0), baseColor, metallic);
    vec3 diffuseColor = surface.diffuseColor;
    surface.diffuseColor = diffuseColor * (1.0 - transmission);
    vec3 color = evaluateIbl(surface, n, v);

    float etaIR = 1.0 / ior;
    vec3 r = -v;
    float noRIn = dot(n, r);
    float sin2ThetaIn = 1.0 - noRIn * noRIn;
    float k = 1.0 - etaIR * etaIR * sin2ThetaIn;
    vec3 rr = etaIR * r - (etaIR * noRIn + sqrt(max(k, 0.0))) * n;
    float noR = dot(n, rr);
    float d = uAbsorptionThickness.w * -noR;
    vec3 exitPosition = vWorld + rr * d;

    float perceptualRoughness = mix(roughness, 0.0, clamp(etaIR * 3.0 - 2.0, 0.0, 1.0));
    float lod = max(0.0, (2.0 * log2(perceptualRoughness) + uParams.w) * 0.8614);

    vec4 p = uViewProjection * vec4(exitPosition, 1.0);
    vec2 uv = p.xy * (0.5 / p.w) + 0.5;
    vec3 ft = samplePyramid(uv, lod);
    ft *= clamp(exp(-uAbsorptionThickness.rgb * d), 0.0, 1.0);

    vec3 e = mix(surface.dfg.xxx, surface.dfg.yyy, surface.f0);
    ft *= 1.0 - e;
    ft *= diffuseColor;
    color += ft * transmission;

    oColor = vec4(color, 1.0);
}
