#version 450

layout(location = 0) in vec3 aPosition;

layout(set = 0, binding = 0, std140) uniform Marker
{
    mat4 uModelViewProjection;
    vec4 uColor;
};

void main()
{
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
