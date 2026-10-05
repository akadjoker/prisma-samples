#version 450

layout(triangles, equal_spacing, ccw) in;

void main()
{
    vec3 t = gl_TessCoord;
    vec4 p = t.x * gl_in[0].gl_Position + t.y * gl_in[1].gl_Position + t.z * gl_in[2].gl_Position;
    if (t.z < 0.01 && abs(t.x - t.y) < 0.01) p.y += 0.5;
    gl_Position = p;
}
