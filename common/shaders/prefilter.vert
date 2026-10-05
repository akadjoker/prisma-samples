#version 450

layout(location = 0) out vec2 vNdc;

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vNdc = corner * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.5, 1.0);
}
