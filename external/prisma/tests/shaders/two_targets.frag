#version 450

layout(location = 0) out vec4 oFirst;
layout(location = 1) out vec4 oSecond;

void main()
{
    oFirst = vec4(1.0, 0.0, 0.0, 1.0);
    oSecond = vec4(0.0, 1.0, 0.0, 1.0);
}
