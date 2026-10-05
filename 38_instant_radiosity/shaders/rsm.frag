#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec3 vClip;

layout(set = 0, binding = 2, std140) uniform Object
{
    mat4 uModel;
    vec4 uTint;
    vec4 uMaterial;
};

layout(set = 1, binding = 3) uniform sampler2D uAlbedo;

// The reflective shadow map: what the light sees, the position, the normal and the colour of
// every surface point, next to its depth.
layout(location = 0) out vec4 oPosition;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oAlbedo;

void main()
{
    oPosition = vec4(vWorld, 1.0);
    oNormal = vec4(normalize(vNormal), 0.0);
    oAlbedo = vec4(texture(uAlbedo, vUv).rgb * uTint.rgb, 1.0);
}
