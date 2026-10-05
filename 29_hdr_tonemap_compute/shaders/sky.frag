#version 450

layout(location = 0) in vec2 vNdc;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    mat4 uModel;
    vec4 uCameraPosition;
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform samplerCube uEnvironment;

layout(location = 0) out vec4 oColor;

void main()
{
    vec4 farPoint = uInverseViewProjection * vec4(vNdc, 1.0, 1.0);
    vec3 direction = farPoint.xyz / farPoint.w - uCameraPosition.xyz;
    oColor = vec4(texture(uEnvironment, direction).rgb * uParams.x, 1.0);
}
