#version 450

layout(location = 0) in vec3 vRay;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 1, binding = 0) uniform samplerCube uSky;

layout(location = 0) out vec4 oColor;

void main()
{
    oColor = vec4(textureLod(uSky, normalize(vRay), uExposure.y).rgb * uExposure.x, 1.0);
}
