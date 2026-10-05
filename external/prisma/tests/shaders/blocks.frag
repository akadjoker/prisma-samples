#version 450

layout(set = 0, binding = 0, std140) uniform A
{
    vec4 a;
};
layout(set = 0, binding = 1, std140) uniform B
{
    vec4 b;
};
layout(set = 0, binding = 2, std140) uniform C
{
    vec4 c;
};
layout(set = 0, binding = 3, std140) uniform D
{
    vec4 d;
};
layout(set = 0, binding = 5, std140) uniform E
{
    vec4 e;
};
layout(set = 0, binding = 9, std140) uniform F
{
    vec4 f;
};

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(a.x + f.x, b.y + e.y, c.z + d.z, 1.0);
}
