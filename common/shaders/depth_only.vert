#version 450

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
    vec4 uSunDirection;
    vec4 uSunColorIntensity;
};

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    mat4 uNormalMatrix;
};

invariant gl_Position;

void main()
{
    gl_Position = uViewProjection * (uModel * vec4(aPosition, 1.0));
}
