#version 450

layout(vertices = 16) out;

layout(set = 0, binding = 0, std140) uniform Surface
{
    mat4 uViewProjection;
    vec4 uLevel;
    vec4 uLightDirection;
};

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
        gl_TessLevelOuter[0] = uLevel.x;
        gl_TessLevelOuter[1] = uLevel.x;
        gl_TessLevelOuter[2] = uLevel.x;
        gl_TessLevelOuter[3] = uLevel.x;
        gl_TessLevelInner[0] = uLevel.x;
        gl_TessLevelInner[1] = uLevel.x;
    }
}
