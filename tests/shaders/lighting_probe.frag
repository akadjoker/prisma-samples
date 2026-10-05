#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../common/shaders/lighting.glsl"
#include "lights_array.glsl"

layout(set = 0, binding = 0, std140) uniform Probe
{
    vec4 uMode;
    vec4 uA;
    vec4 uB;
    vec4 uC;
    vec4 uD;
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
    float result = 0.0;
    int mode = int(uMode.x + 0.5);
    int channel = int(uMode.y + 0.5);
    if (mode == 0)
        result = distributionGgx(uA.x, uA.y);
    else if (mode == 1)
        result = visibilitySmithGgxCorrelated(uA.x, uA.y, uA.z);
    else
    {
        vec3 n = normalize(uA.xyz);
        vec3 v = normalize(uB.xyz);
        PbrSurface surface;
        setClearCoatSurface(surface, uD.xyz, uA.w, uB.w, uC.w, uD.w);
        surface.noV = max(dot(n, v), kMinNoV);
        surface.dfg = vec3(0.0);
        surface.energyCompensation = vec3(1.0);
        vec3 color = evaluateLights(surface, n, v, uC.xyz);
        result = color[channel];
    }
    oColor = packFloat(result / uMode.z);
}
