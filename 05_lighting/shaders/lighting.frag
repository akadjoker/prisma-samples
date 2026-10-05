#version 450

layout(location = 0) in vec3 vNormal;

layout(set = 0, binding = 0, std140) uniform Scene
{
    mat4 uModelViewProjection;
    mat4 uModel;
    vec4 uLightDirection[2];
    vec4 uLightColor[2];
    vec4 uAmbient;
};

layout(location = 0) out vec4 oColor;

void main()
{
    vec3 normal = normalize(vNormal);
    vec3 light = uAmbient.rgb;
    for (int i = 0; i < 2; ++i)
        light += uLightColor[i].rgb * max(dot(normal, uLightDirection[i].xyz), 0.0);
    oColor = vec4(vec3(0.8) * light, 1.0);
}
