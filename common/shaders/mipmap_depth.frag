#version 450

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Params
{
    vec4 uParams;
};

layout(set = 1, binding = 0) uniform sampler2D uDepth;

void main()
{
    ivec2 icoord = ivec2(gl_FragCoord.xy);
    gl_FragDepth = texelFetch(uDepth, 2 * icoord + ivec2(icoord.y & 1, icoord.x & 1),
            0).r;
}
