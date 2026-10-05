#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uScreen;
    vec4 uLight;
};

layout(set = 2, binding = 0, std430) readonly buffer Heads
{
    uint uHeads[];
};

layout(set = 2, binding = 1, std430) readonly buffer Nodes
{
    uvec4 uNodes[];
};

layout(location = 0) out vec4 oColor;

const uint kEnd = 0xFFFFFFFFu;
const int kMaxLayers = 32;

void main()
{
    uint pixel = uint(gl_FragCoord.y) * uint(uScreen.x) + uint(gl_FragCoord.x);
    uint node = uHeads[pixel];

    uint colors[kMaxLayers];
    float depths[kMaxLayers];
    int count = 0;
    while (node != kEnd && count < kMaxLayers)
    {
        uvec4 entry = uNodes[node];
        float depth = uintBitsToFloat(entry.y);
        int slot = count;
        while (slot > 0 && depths[slot - 1] < depth)
        {
            depths[slot] = depths[slot - 1];
            colors[slot] = colors[slot - 1];
            --slot;
        }
        depths[slot] = depth;
        colors[slot] = entry.x;
        node = entry.z;
        ++count;
    }

    vec3 premultiplied = vec3(0.0);
    float coverage = 0.0;
    for (int i = 0; i < count; ++i)
    {
        vec4 color = unpackUnorm4x8(colors[i]);
        premultiplied = color.rgb * color.a + premultiplied * (1.0 - color.a);
        coverage = color.a + coverage * (1.0 - color.a);
    }
    oColor = vec4(premultiplied, coverage);
}
