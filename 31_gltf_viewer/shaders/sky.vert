#version 450

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(location = 0) out vec3 vRay;

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vec2 ndc = corner * 2.0 - 1.0;
    vec4 far = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
    vRay = far.xyz / far.w - uCamera.xyz;
    gl_Position = vec4(ndc, 0.99999, 1.0);
}
