#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../common/shaders/ltc.glsl"

layout(set = 0, binding = 0, std140) uniform Probe
{
    vec4 uMode;
    vec4 uNormal;
    vec4 uView;
    vec4 uPosition;
    vec4 uCorners[4];
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
    vec3 n = normalize(uNormal.xyz);
    vec3 v = normalize(uView.xyz);
    vec3 corners[4];
    for (int i = 0; i < 4; ++i)
        corners[i] = uCorners[i].xyz - uPosition.xyz;
    bool twoSided = uMode.y > 0.5;
    float result;
    if (uMode.x < 0.5)
        result = ltcPolygon(mat3(1.0), n, v, corners, twoSided);
    else
        result = ltcPolygon(ltcInverse(uMode.w, dot(n, v)), n, v, corners, twoSided);
    oColor = packFloat(result);
}
