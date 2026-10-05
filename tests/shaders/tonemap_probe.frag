#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../common/shaders/tonemap.glsl"

layout(set = 0, binding = 0, std140) uniform Probe
{
    vec4 uMode;
    vec4 uColor;
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
    int channel = int(uMode.y + 0.5);
    vec3 mapped = tonemapAcesLegacy(uColor.rgb);
    if (uMode.x > 0.5) mapped = linearToSrgb(mapped);
    oColor = packFloat(mapped[channel]);
}
