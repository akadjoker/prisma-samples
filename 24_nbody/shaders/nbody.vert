#version 450

struct Body
{
    vec4 position;
    vec4 velocity;
};

layout(set = 2, binding = 0, std430) readonly buffer Bodies
{
    Body uBodies[];
};

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uSize;
    vec4 uOptions;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vColor;

void main()
{
    Body body = uBodies[gl_InstanceIndex];
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1)) * 2.0 - 1.0;

    vec4 clip = uViewProjection * vec4(body.position.xyz, 1.0);
    clip.xy += corner * uSize.xy * clip.w;

    float heat = clamp(length(body.velocity.xyz) * uOptions.y, 0.0, 1.0);
    vec3 slow = vec3(0.25, 0.4, 1.0);
    vec3 middle = vec3(1.0, 0.75, 0.45);
    vec3 fast = vec3(1.0, 0.95, 0.9);
    vColor = (heat < 0.5 ? mix(slow, middle, heat * 2.0) : mix(middle, fast, heat * 2.0 - 1.0)) *
             uOptions.x;
    vUv = corner;
    gl_Position = clip;
}
