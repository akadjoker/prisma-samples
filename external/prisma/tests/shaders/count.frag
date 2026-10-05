#version 450

layout(set = 2, binding = 0, std430) buffer Counter
{
    uint uCount;
};

layout(location = 0) out vec4 oColor;

void main()
{
    atomicAdd(uCount, 1u);
    oColor = vec4(1.0);
}
