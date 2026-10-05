#version 450

layout(quads, fractional_odd_spacing, ccw) in;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    vec4 uCamera;
    vec4 uLightDirection;
    vec4 uParams;
    vec4 uFog;
};

layout(set = 1, binding = 1) uniform sampler2D uNormalHeight;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vWorld;

void main()
{
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;
    vec3 p3 = gl_in[3].gl_Position.xyz;
    vec3 position = mix(mix(p0, p1, u), mix(p3, p2, u), v);

    vec2 uv = position.xz * uParams.w;
    float level = clamp(uParams.y / max(distance(position, uCamera.xyz), 0.01), 1.0, uParams.z);
    float lod = max(log2(uParams.w * 256.0 / level), 0.0);
    position.y += textureLod(uNormalHeight, uv, lod).a * uParams.x;

    vUv = uv;
    vWorld = position;
    gl_Position = uViewProjection * vec4(position, 1.0);
}
