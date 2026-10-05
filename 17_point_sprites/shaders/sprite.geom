#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uView;
    mat4 uProjection;
    vec4 uParams;
};

layout(location = 0) in vec3 vViewPosition[];
layout(location = 1) in float vSize[];
layout(location = 2) in vec4 vColor[];

layout(location = 0) out vec2 gUv;
layout(location = 1) out vec4 gColor;

void main()
{
    vec3 center = vViewPosition[0];
    if (center.z > -0.1)
        return;

    float size = vSize[0];
    for (int i = 0; i < 4; ++i)
    {
        vec2 corner = vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0);
        gUv = corner;
        gColor = vColor[0];
        gl_Position = uProjection * vec4(center + vec3(corner * size, 0.0), 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
