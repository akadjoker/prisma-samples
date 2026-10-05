#version 450

layout(vertices = 3) out;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uModel;
    vec4 uCamera;
    vec4 uLightDirection;
    vec4 uLevels;
};

layout(location = 0) in vec3 vNormal[];
layout(location = 1) in vec2 vUv[];

layout(location = 0) out vec3 tNormal[];
layout(location = 1) out vec2 tUv[];
layout(location = 2) patch out vec3 pPosition[7];
layout(location = 9) patch out vec3 pNormal[3];

float levelAt(vec3 objectPosition)
{
    vec3 world = (uModel * vec4(objectPosition, 1.0)).xyz;
    float distanceToCamera = max(distance(world, uCamera.xyz), 0.01);
    return clamp(uLevels.x * uLevels.y / distanceToCamera, 1.0, uLevels.z);
}

vec3 edgePoint(vec3 pi, vec3 pj, vec3 ni)
{
    float w = dot(pj - pi, ni);
    return (2.0 * pi + pj - w * ni) / 3.0;
}

vec3 edgeNormal(vec3 pi, vec3 pj, vec3 ni, vec3 nj)
{
    vec3 edge = pj - pi;
    float v = 2.0 * dot(edge, ni + nj) / max(dot(edge, edge), 1e-12);
    return normalize(ni + nj - v * edge);
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tNormal[gl_InvocationID] = vNormal[gl_InvocationID];
    tUv[gl_InvocationID] = vUv[gl_InvocationID];

    if (gl_InvocationID == 0)
    {
        vec3 p0 = gl_in[0].gl_Position.xyz;
        vec3 p1 = gl_in[1].gl_Position.xyz;
        vec3 p2 = gl_in[2].gl_Position.xyz;
        vec3 n0 = normalize(vNormal[0]);
        vec3 n1 = normalize(vNormal[1]);
        vec3 n2 = normalize(vNormal[2]);

        vec3 b210 = edgePoint(p0, p1, n0);
        vec3 b120 = edgePoint(p1, p0, n1);
        vec3 b021 = edgePoint(p1, p2, n1);
        vec3 b012 = edgePoint(p2, p1, n2);
        vec3 b102 = edgePoint(p2, p0, n2);
        vec3 b201 = edgePoint(p0, p2, n0);
        vec3 edges = (b210 + b120 + b021 + b012 + b102 + b201) / 6.0;
        vec3 corners = (p0 + p1 + p2) / 3.0;
        vec3 b111 = edges + (edges - corners) * 0.5;

        pPosition[0] = b210;
        pPosition[1] = b120;
        pPosition[2] = b021;
        pPosition[3] = b012;
        pPosition[4] = b102;
        pPosition[5] = b201;
        pPosition[6] = b111;
        pNormal[0] = edgeNormal(p0, p1, n0, n1);
        pNormal[1] = edgeNormal(p1, p2, n1, n2);
        pNormal[2] = edgeNormal(p2, p0, n2, n0);

        float level0 = levelAt((p1 + p2) * 0.5);
        float level1 = levelAt((p2 + p0) * 0.5);
        float level2 = levelAt((p0 + p1) * 0.5);
        gl_TessLevelOuter[0] = level0;
        gl_TessLevelOuter[1] = level1;
        gl_TessLevelOuter[2] = level2;
        gl_TessLevelInner[0] = max(level0, max(level1, level2));
    }
}
