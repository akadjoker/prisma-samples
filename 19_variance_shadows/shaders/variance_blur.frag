#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Blur
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec2 oMoments;

void main()
{
    int radius = int(uParams.z);
    vec2 sum = vec2(0.0);
    for (int i = -radius; i <= radius; ++i) sum += texture(uSource, vUv + uParams.xy * float(i)).xy;
    oMoments = sum / float(2 * radius + 1);
}
