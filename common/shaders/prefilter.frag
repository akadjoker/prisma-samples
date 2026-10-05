#version 450

layout(location = 0) in vec2 vNdc;

layout(set = 0, binding = 0, std140) uniform Params
{
    vec4 uA;
    vec4 uB;
};

layout(set = 1, binding = 0) uniform samplerCube uSource;

layout(location = 0) out vec4 oColor;

const float kPi = 3.14159265358979;

vec3 faceDirection(int face, vec2 st)
{
    vec3 d;
    if (face == 0)
        d = vec3(1.0, -st.y, -st.x);
    else if (face == 1)
        d = vec3(-1.0, -st.y, st.x);
    else if (face == 2)
        d = vec3(st.x, 1.0, st.y);
    else if (face == 3)
        d = vec3(st.x, -1.0, -st.y);
    else if (face == 4)
        d = vec3(st.x, -st.y, 1.0);
    else
        d = vec3(-st.x, -st.y, -1.0);
    return normalize(d);
}

vec2 hammersley(uint i, float inverseCount)
{
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return vec2(float(i) * inverseCount, float(bits) * 2.3283064365386963e-10);
}

vec3 importanceSampleGgx(vec2 u, float a)
{
    float phi = 2.0 * kPi * u.x;
    float cosTheta2 = (1.0 - u.y) / (1.0 + (a + 1.0) * ((a - 1.0) * u.y));
    float cosTheta = sqrt(cosTheta2);
    float sinTheta = sqrt(1.0 - cosTheta2);
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float distributionGgx(float noh, float a)
{
    float f = (a - 1.0) * ((a + 1.0) * (noh * noh)) + 1.0;
    return (a * a) / (kPi * f * f);
}

float rotationAngle(vec2 pixel)
{
    float noise = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
    return (noise * 2.0 - 1.0) * kPi;
}

void main()
{
    int face = int(uA.x + 0.5);
    float roughness = uA.y;
    float sampleCount = uA.z;
    float maxLevel = uA.w;
    float log4OmegaP = uB.x;

    vec3 n = faceDirection(face, vNdc);
    if (roughness == 0.0)
    {
        oColor = vec4(textureLod(uSource, n, 0.0).rgb, 1.0);
        return;
    }

    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);
    float angle = rotationAngle(gl_FragCoord.xy);
    float c = cos(angle);
    float s = sin(angle);

    float inverseCount = 1.0 / sampleCount;
    int count = int(sampleCount);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < count; ++i)
    {
        vec2 u = hammersley(uint(i), inverseCount);
        vec3 h = importanceSampleGgx(u, roughness);
        float noh = h.z;
        float nol = 2.0 * noh * noh - 1.0;
        if (nol > 0.0)
        {
            vec3 l = vec3(2.0 * noh * h.x, 2.0 * noh * h.y, nol);
            float pdf = distributionGgx(noh, roughness) * 0.25;
            float omegaS = 1.0 / (sampleCount * pdf);
            float mip = clamp(0.5 * log2(omegaS) - log4OmegaP + 1.0, 0.0, maxLevel);
            vec3 direction = tangent * (l.x * c - l.y * s) + bitangent * (l.x * s + l.y * c) + n * l.z;
            sum += textureLod(uSource, direction, mip).rgb * nol;
            weight += nol;
        }
    }
    oColor = vec4(sum / weight, 1.0);
}
