#ifndef PBR_SURFACE_GLSL
#define PBR_SURFACE_GLSL

const float kMinPerceptualRoughness = 0.045;
const float kMinNoV = 1e-4;

struct PbrSurface
{
    vec3 diffuseColor;
    vec3 f0;
    float perceptualRoughness;
    float roughness;
    float noV;
    vec3 dfg;
    vec3 energyCompensation;
    float clearCoat;
    float clearCoatPerceptualRoughness;
    float clearCoatRoughness;
};

const float kPi = 3.14159265358979;

vec3 f0ClearCoatToSurface(vec3 f0)
{
    return clamp(f0 * (f0 * (0.941892 - 0.263008 * f0) + 0.346479) - 0.0285998, 0.0, 1.0);
}

void setClearCoatSurface(inout PbrSurface surface, vec3 baseColor, float metallic,
        float perceptualRoughness, float clearCoat, float clearCoatPerceptualRoughness)
{
    surface.clearCoat = clearCoat;
    surface.clearCoatPerceptualRoughness =
            clamp(clearCoatPerceptualRoughness, kMinPerceptualRoughness, 1.0);
    surface.clearCoatRoughness =
            surface.clearCoatPerceptualRoughness * surface.clearCoatPerceptualRoughness;
    surface.diffuseColor = baseColor * (1.0 - metallic);
    vec3 f0 = baseColor * metallic + vec3(0.04 * (1.0 - metallic));
    surface.f0 = mix(f0, f0ClearCoatToSurface(f0), clearCoat);
    float baseRoughness = clamp(perceptualRoughness, kMinPerceptualRoughness, 1.0);
    surface.perceptualRoughness =
            mix(baseRoughness, max(baseRoughness, surface.clearCoatPerceptualRoughness), clearCoat);
    surface.roughness = surface.perceptualRoughness * surface.perceptualRoughness;
}

#endif
