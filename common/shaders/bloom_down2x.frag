#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Params
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

float max3(vec3 v)
{
    return max(v.x, max(v.y, v.z));
}

void main()
{
    vec2 size = vec2(textureSize(uSource, 0));
    vec2 texelSize = vec2(1.0) / size;
    vec2 offset = vec2(0.5);
    vec2 uv = vUv * size + offset;
    vec2 base = (floor(uv) - offset) * texelSize;
    vec2 st = fract(uv);
    vec2 uw = vec2(3.0 - 2.0 * st.x, 1.0 + 2.0 * st.x);
    vec2 vw = vec2(3.0 - 2.0 * st.y, 1.0 + 2.0 * st.y);
    vec2 u = vec2((2.0 - st.x) / uw.x - 1.0, st.x / uw.y + 1.0) * texelSize.x;
    vec2 v = vec2((2.0 - st.y) / vw.x - 1.0, st.y / vw.y + 1.0) * texelSize.y;
    vec3 c0 = textureLod(uSource, base + vec2(u.x, v.x), 0.0).rgb;
    vec3 c1 = textureLod(uSource, base + vec2(u.y, v.x), 0.0).rgb;
    vec3 c2 = textureLod(uSource, base + vec2(u.x, v.y), 0.0).rgb;
    vec3 c3 = textureLod(uSource, base + vec2(u.y, v.y), 0.0).rgb;

    float w0 = uw.x * vw.x * (1.0 / 16.0);
    float w1 = uw.y * vw.x * (1.0 / 16.0);
    float w2 = uw.x * vw.y * (1.0 / 16.0);
    float w3 = uw.y * vw.y * (1.0 / 16.0);

    if (uParams.y > 0.0)
    {
        w0 /= (1.0 + max3(c0));
        w1 /= (1.0 + max3(c1));
        w2 /= (1.0 + max3(c2));
        w3 /= (1.0 + max3(c3));
        float w = 1.0 / (w0 + w1 + w2 + w3);
        w0 *= w;
        w1 *= w;
        w2 *= w;
        w3 *= w;
    }

    vec3 c = c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3;

    if (uParams.x > 0.0)
    {
        c = max(vec3(0.0), c - 1.0);
        float f = max3(c);
        c *= 1.0 / (1.0 + f * uParams.z);
    }

    oColor = vec4(c, 1.0);
}
