#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(set = 0, binding = 0, std140) uniform Block
{
    mat4 uMvp;
    mat4 uMv;
    mat4 uProj;
    vec4 uLightView;
    vec4 uParams;
};

invariant gl_Position;

void main()
{
    vec3 normal = mat3(uMv) * aNormal;
    vec4 position = uMv * vec4(aPosition, 1.0);
    vec3 fromLight = position.xyz - uLightView.xyz;

    if (dot(normal, -fromLight) < 0.0)
    {
        position = vec4(fromLight, 0.0);
        gl_Position = uProj * position;
    }
    else
    {
        gl_Position = uMvp * vec4(aPosition, 1.0);
    }
}
