#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../common/shaders/lights_clustered.glsl"

layout(set = 0, binding = 0, std140) uniform Probe
{
    vec4 uMode;
    vec4 uA;
    vec4 uB;
    vec4 uC;
    vec4 uD;
    vec4 uE;
};

layout(location = 0) out vec4 oColor;

vec4 packFloat(float value)
{
    value = clamp(value, 0.0, 0.99999994);
    vec4 encoded = fract(value * vec4(1.0, 255.0, 65025.0, 16581375.0));
    encoded -= encoded.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
    return encoded;
}

void main()
{
    int mode = int(uMode.x + 0.5);
    float result = 0.0;
    if (mode == 2)
        result = float(froxelIndex(uE.xyz));
    else
    {
        vec3 n = normalize(uA.xyz);
        vec3 v = normalize(uB.xyz);
        PbrSurface surface;
        surface.diffuseColor = uD.xyz * (1.0 - uA.w);
        surface.f0 = uD.xyz * uA.w + vec3(0.04 * (1.0 - uA.w));
        surface.perceptualRoughness = uB.w;
        surface.roughness = uB.w * uB.w;
        surface.noV = max(dot(n, v), kMinNoV);
        surface.dfg = vec3(0.0);
        surface.energyCompensation = vec3(1.0);
        surface.clearCoat = 0.0;
        surface.clearCoatPerceptualRoughness = 1.0;
        surface.clearCoatRoughness = 1.0;
        vec3 color = mode == 0 ? evaluateLights(surface, n, v, uC.xyz, uE.xyz)
                               : evaluateAllLights(surface, n, v, uC.xyz);
        result = color.r + color.g + color.b;
    }
    oColor = packFloat(result / uMode.z);
}
