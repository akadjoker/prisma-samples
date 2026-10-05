#version 450

layout(triangles_adjacency) in;
layout(triangle_strip, max_vertices = 3) out;

void main()
{
    for (int i = 0; i < 6; i += 2)
    {
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
