#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aSize;
layout(location = 2) in vec4 aColor;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uView;
    mat4 uProjection;
    vec4 uParams;
};

layout(location = 0) out vec3 vViewPosition;
layout(location = 1) out float vSize;
layout(location = 2) out vec4 vColor;

void main()
{
    float radius = length(aPosition.xz);
    float angle = uParams.x * 0.6 / (0.6 + radius);
    float c = cos(angle);
    float s = sin(angle);
    vec3 rotated = vec3(c * aPosition.x - s * aPosition.z, aPosition.y, s * aPosition.x + c * aPosition.z);
    vec4 viewPosition = uView * vec4(rotated, 1.0);
    vViewPosition = viewPosition.xyz;
    vSize = aSize;
    vColor = aColor;
    gl_Position = uProjection * viewPosition;
}
