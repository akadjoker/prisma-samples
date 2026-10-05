#version 450

layout(location = 0) in vec2 vUv;

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

void main()
{
    vec2 uv = vUv;
    vec2 size = vec2(1.0) / vec2(textureSize(uSource, 0));

    float o = 1.5 + 0.261629;
    float wa = 7.46602 / 32.0;
    float wb = 1.0 - wa * 2.0;
    float wab = wa * wb;
    float waa = wa * wa;
    float wbb = wb * wb;

    size *= o;

    vec3 c = textureLod(uSource, uv + vec2(0.0), 0.0).rgb;
    vec3 l = textureLod(uSource, uv + vec2(-size.x, 0.0), 0.0).rgb;
    vec3 r = textureLod(uSource, uv + vec2(size.x, 0.0), 0.0).rgb;
    vec3 b = textureLod(uSource, uv + vec2(0.0, -size.y), 0.0).rgb;
    vec3 t = textureLod(uSource, uv + vec2(0.0, size.y), 0.0).rgb;
    vec3 lb = textureLod(uSource, uv + vec2(-size.x, -size.y), 0.0).rgb;
    vec3 rb = textureLod(uSource, uv + vec2(size.x, -size.y), 0.0).rgb;
    vec3 lt = textureLod(uSource, uv + vec2(-size.x, size.y), 0.0).rgb;
    vec3 rt = textureLod(uSource, uv + vec2(size.x, size.y), 0.0).rgb;

    oColor = vec4(c * wbb + l * wab + r * wab + b * wab + t * wab + lb * waa + rb * waa +
                          lt * waa + rt * waa, 1.0);
}
