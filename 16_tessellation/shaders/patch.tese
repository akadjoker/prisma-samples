#version 450

layout(quads, fractional_odd_spacing, ccw) in;

layout(set = 0, binding = 0, std140) uniform Surface
{
    mat4 uViewProjection;
    vec4 uLevel;
    vec4 uLightDirection;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out float vHeight;

vec4 basis(float t)
{
    float s = 1.0 - t;
    return vec4(s * s * s, 3.0 * s * s * t, 3.0 * s * t * t, t * t * t);
}

vec4 basisDerivative(float t)
{
    float s = 1.0 - t;
    return vec4(-3.0 * s * s, 3.0 * s * s - 6.0 * s * t, 6.0 * s * t - 3.0 * t * t, 3.0 * t * t);
}

void main()
{
    vec4 bu = basis(gl_TessCoord.x);
    vec4 bv = basis(gl_TessCoord.y);
    vec4 du = basisDerivative(gl_TessCoord.x);
    vec4 dv = basisDerivative(gl_TessCoord.y);

    vec3 position = vec3(0.0);
    vec3 tangentU = vec3(0.0);
    vec3 tangentV = vec3(0.0);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            vec3 point = gl_in[row * 4 + column].gl_Position.xyz;
            position += bu[column] * bv[row] * point;
            tangentU += du[column] * bv[row] * point;
            tangentV += bu[column] * dv[row] * point;
        }
    }

    vNormal = normalize(cross(tangentV, tangentU));
    vHeight = position.y;
    gl_Position = uViewProjection * vec4(position, 1.0);
}
