#version 450

struct Element
{
    int i;
    float f;
};

layout(set = 2, binding = 0, std430) readonly buffer Result
{
    Element uResult[];
};

layout(set = 0, binding = 0, std140) uniform Frame
{
    vec4 uView;
    vec4 uParams;
};

layout(location = 0) out vec3 vColor;

void main()
{
    int index = gl_InstanceIndex;
    Element element = uResult[index];
    int columns = int(uParams.x);
    vec2 cell = vec2(float(index % columns), float(index / columns));
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1)) * 2.0 - 1.0;

    float pattern = float(element.i - index) / uParams.y;
    float height = clamp(0.5 + 0.25 * element.f, 0.0, 1.0);
    float shimmer = 0.9 + 0.1 * sin(uParams.z * 2.0 + element.f * 3.0);

    vec2 center = (cell - 0.5 * float(columns - 1)) * uView.xy;
    gl_Position = vec4(center + corner * uView.xy * 0.5, 0.0, 1.0);

    vec3 palette = 0.5 + 0.5 * cos(6.2831853 * (pattern + vec3(0.0, 0.33, 0.67)));
    vColor = palette * (0.25 + 0.75 * height) * shimmer;
}
