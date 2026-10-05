#version 450

layout(vertices = 4) out;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uCamera;
    vec4 uLightDirection;
    vec4 uParams;
    vec4 uFog;
};

float levelAt(vec3 position)
{
    float distanceToCamera = max(distance(position, uCamera.xyz), 0.01);
    return clamp(uParams.y / distanceToCamera, 1.0, uParams.z);
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
        vec3 p0 = gl_in[0].gl_Position.xyz;
        vec3 p1 = gl_in[1].gl_Position.xyz;
        vec3 p2 = gl_in[2].gl_Position.xyz;
        vec3 p3 = gl_in[3].gl_Position.xyz;

        float left = levelAt((p0 + p3) * 0.5);
        float bottom = levelAt((p0 + p1) * 0.5);
        float right = levelAt((p1 + p2) * 0.5);
        float top = levelAt((p3 + p2) * 0.5);

        gl_TessLevelOuter[0] = left;
        gl_TessLevelOuter[1] = bottom;
        gl_TessLevelOuter[2] = right;
        gl_TessLevelOuter[3] = top;
        gl_TessLevelInner[0] = max(bottom, top);
        gl_TessLevelInner[1] = max(left, right);
    }
}
