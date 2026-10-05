#version 450

layout(set = 2, binding = 0, std430) readonly buffer Values
{
    uint uValues[];
};

layout(set = 0, binding = 0, std140) uniform Frame
{
    vec4 uParams;
};

layout(location = 0) out vec3 vColor;

void main()
{
    uint index = uint(gl_InstanceIndex);
    float value = float(uValues[index]) * (1.0 / 4294967296.0);
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1)) * 2.0 - 1.0;

    float x = (float(index) + 0.5) / uParams.x * 2.0 - 1.0;
    float y = value * 2.0 - 1.0;
    vec2 center = vec2(x, y) * 0.96;
    gl_Position = vec4(center + corner * uParams.yz, 0.0, 1.0);

    vec3 palette = 0.5 + 0.5 * cos(6.2831853 * (value * 0.85 + vec3(0.0, 0.33, 0.67)));
    vColor = palette * uParams.w;
}
