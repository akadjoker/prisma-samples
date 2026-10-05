#version 450

struct Particle
{
    vec2 position;
    vec2 velocity;
    vec2 density;
    vec2 acceleration;
};

layout(set = 2, binding = 0, std430) readonly buffer Particles
{
    Particle uParticles[];
};

layout(set = 0, binding = 0, std140) uniform Frame
{
    vec4 uView;
    vec4 uParams;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vColor;

void main()
{
    Particle particle = uParticles[gl_InstanceIndex];
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1)) * 2.0 - 1.0;

    vec2 center = particle.position * uView.xy + uView.zw;
    gl_Position = vec4(center + corner * uParams.x * uView.xy, 0.0, 1.0);

    float speed = clamp(length(particle.velocity) * uParams.y, 0.0, 1.0);
    float thin = clamp((0.6 - particle.density.x / uParams.z) * 3.0, 0.0, 1.0);
    vec3 deep = vec3(0.04, 0.22, 0.62);
    vec3 foam = vec3(0.75, 0.93, 1.0);
    vColor = mix(deep, foam, clamp(0.8 * speed * speed + thin, 0.0, 1.0));
    vUv = corner;
}
