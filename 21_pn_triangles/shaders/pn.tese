#version 450

layout(triangles, fractional_odd_spacing, ccw) in;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uModel;
    vec4 uCamera;
    vec4 uLightDirection;
    vec4 uLevels;
};

layout(location = 0) in vec3 tNormal[];
layout(location = 1) in vec2 tUv[];
layout(location = 2) patch in vec3 pPosition[7];
layout(location = 9) patch in vec3 pNormal[3];

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vWorld;

void main()
{
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    float w = gl_TessCoord.z;

    vec3 b300 = gl_in[0].gl_Position.xyz;
    vec3 b030 = gl_in[1].gl_Position.xyz;
    vec3 b003 = gl_in[2].gl_Position.xyz;

    vec3 position = b300 * (u * u * u) + b030 * (v * v * v) + b003 * (w * w * w) +
                    pPosition[0] * (3.0 * u * u * v) + pPosition[1] * (3.0 * u * v * v) +
                    pPosition[2] * (3.0 * v * v * w) + pPosition[3] * (3.0 * v * w * w) +
                    pPosition[4] * (3.0 * u * w * w) + pPosition[5] * (3.0 * u * u * w) +
                    pPosition[6] * (6.0 * u * v * w);

    vec3 normal = normalize(tNormal[0]) * (u * u) + normalize(tNormal[1]) * (v * v) +
                  normalize(tNormal[2]) * (w * w) + pNormal[0] * (u * v) + pNormal[1] * (v * w) +
                  pNormal[2] * (w * u);

    vec4 world = uModel * vec4(position, 1.0);
    vWorld = world.xyz;
    vNormal = normalize(mat3(uModel) * normal);
    vUv = tUv[0] * u + tUv[1] * v + tUv[2] * w;
    gl_Position = uViewProjection * world;
}
