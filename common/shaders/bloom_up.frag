#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Params
{
    vec4 uResolution;
};

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 uv = vUv;
    vec4 d = vec4(uResolution.zw, -uResolution.zw) * 0.5;
    vec3 c = textureLod(uSource, uv + d.zw, 0.0).rgb;
    c += textureLod(uSource, uv + d.xw, 0.0).rgb;
    c += textureLod(uSource, uv + d.xy, 0.0).rgb;
    c += textureLod(uSource, uv + d.zy, 0.0).rgb;
    oColor = vec4(c * 0.25, 1.0);
}
