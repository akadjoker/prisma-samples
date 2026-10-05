#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uScene;

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 color = texture(uScene, vUv).rgb;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float scale = max(luma - uParams.x, 0.0) / max(luma, 0.0001);
    oColor = vec4(color * scale, 1.0);
}
