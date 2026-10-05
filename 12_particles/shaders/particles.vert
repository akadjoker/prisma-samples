#version 450

struct Particle
{
    vec4 position;
    vec4 velocity;
};

layout(set = 2, binding = 0, std430) readonly buffer Particles
{
    Particle uParticles[];
};

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uParams;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vColor;

void main()
{
    Particle particle = uParticles[gl_InstanceIndex];
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1)) * 2.0 - 1.0;

    vec4 clip = uViewProjection * vec4(particle.position.xyz, 1.0);
    clip.xy += corner * uParams.xy * clip.w;

    float heat = clamp(length(particle.velocity.xyz) * 0.7, 0.0, 1.0);
    vColor = mix(vec3(0.15, 0.35, 1.0), vec3(1.0, 0.55, 0.15), heat) * uParams.z;
    vUv = corner;
    gl_Position = clip;
}
