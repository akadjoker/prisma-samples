#version 450

layout(early_fragment_tests) in;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uScreen;
    vec4 uLight;
};

layout(set = 0, binding = 1, std140) uniform Object
{
    mat4 uModel;
    vec4 uColor;
};

layout(set = 2, binding = 0, std430) buffer Heads
{
    uint uHeads[];
};

layout(set = 2, binding = 1, std430) buffer Nodes
{
    uvec4 uNodes[];
};

layout(set = 2, binding = 2, std430) buffer Counter
{
    uint uCounter;
};

layout(location = 0) out vec4 oColor;

void main()
{
    float facing = abs(dot(normalize(vNormal), normalize(uLight.xyz)));
    vec4 color = vec4(uColor.rgb * (0.55 + 0.45 * facing), uColor.a);
    oColor = color;

    uint index = atomicAdd(uCounter, 1u);
    if (index < uint(uScreen.z))
    {
        uint pixel = uint(gl_FragCoord.y) * uint(uScreen.x) + uint(gl_FragCoord.x);
        uint previous = atomicExchange(uHeads[pixel], index);
        uNodes[index] = uvec4(packUnorm4x8(color), floatBitsToUint(gl_FragCoord.z), previous, 0u);
    }
}
