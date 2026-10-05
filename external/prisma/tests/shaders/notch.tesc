#version 450

layout(vertices = 3) out;

layout(set = 0, binding = 2, std140) uniform Params
{
    vec4 uColor;
    vec4 uPlace;
};

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
        gl_TessLevelOuter[0] = uPlace.x;
        gl_TessLevelOuter[1] = uPlace.x;
        gl_TessLevelOuter[2] = uPlace.x;
        gl_TessLevelInner[0] = uPlace.x;
    }
}
