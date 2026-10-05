#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(set = 0, binding = 2, std140) uniform Params
{
    vec4 uColor;
    vec4 uPlace;
};

void main()
{
    vec4 center = gl_in[0].gl_Position;
    float extent = uPlace.x;
    gl_Position = center + vec4(-extent, -extent, 0.0, 0.0);
    EmitVertex();
    gl_Position = center + vec4(extent, -extent, 0.0, 0.0);
    EmitVertex();
    gl_Position = center + vec4(-extent, extent, 0.0, 0.0);
    EmitVertex();
    gl_Position = center + vec4(extent, extent, 0.0, 0.0);
    EmitVertex();
    EndPrimitive();
}
