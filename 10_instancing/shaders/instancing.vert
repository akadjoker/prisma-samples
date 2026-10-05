#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aModel0;
layout(location = 3) in vec4 aModel1;
layout(location = 4) in vec4 aModel2;
layout(location = 5) in vec4 aModel3;
layout(location = 6) in vec4 aColor;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

void main()
{
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    vNormal = mat3(model) * aNormal;
    vColor = aColor.rgb;
    gl_Position = uViewProjection * model * vec4(aPosition, 1.0);
}
