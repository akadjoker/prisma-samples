#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

void main()
{
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 offset = uParams.xy;
    vec3 sum = texture(uSource, vUv).rgb * weights[0];
    for (int i = 1; i < 5; ++i)
    {
        sum += texture(uSource, vUv + offset * float(i)).rgb * weights[i];
        sum += texture(uSource, vUv - offset * float(i)).rgb * weights[i];
    }
    oColor = vec4(sum, 1.0);
}
