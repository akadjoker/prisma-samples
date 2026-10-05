#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in vec3 vClip;
layout(location = 3) in vec4 vTangent;
layout(location = 4) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Frame
{
    mat4 uViewProjection;
    mat4 uInverseViewProjection;
    vec4 uCamera;
    vec4 uExposure;
};

layout(set = 0, binding = 7, std140) uniform Material
{
    vec4 uBaseColor;
    vec4 uSurface;
    vec4 uSpecular;
    vec4 uEmissive;
    vec4 uFlags;
    vec4 uModes;
};

layout(set = 1, binding = 2) uniform sampler2D uBaseTexture;
layout(set = 1, binding = 3) uniform sampler2D uSurfaceTexture;
layout(set = 1, binding = 4) uniform sampler2D uNormalTexture;
layout(set = 1, binding = 5) uniform sampler2D uOcclusionTexture;
layout(set = 1, binding = 6) uniform sampler2D uEmissiveTexture;

#define LIGHT_SHADOWS
#include "../../common/shaders/pbr.glsl"
#include "../../common/shaders/light_shadows.glsl"
#include "../../common/shaders/lights_clustered.glsl"
#include "../../common/shaders/tonemap.glsl"

layout(location = 0) out vec4 oColor;

void main()
{
    vec4 base = uBaseColor;
    if (uFlags.x > 0.5)
        base *= texture(uBaseTexture, vUv);
    if (uModes.w > 0.5 && uModes.w < 1.5 && base.a < uSurface.w)
        discard;

    vec3 n = normalize(vNormal);
    vec3 geometricNormal = (uModes.z > 0.5 && !gl_FrontFacing) ? -n : n;
    if (uFlags.z > 0.5)
    {
        vec3 t = normalize(vTangent.xyz);
        vec3 b = cross(n, t) * vTangent.w;
        vec3 sampled = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
        sampled.xy *= uSpecular.w;
        n = normalize(mat3(t, b, n) * sampled);
    }
    if (uModes.z > 0.5 && !gl_FrontFacing)
        n = -n;
    vec3 v = normalize(uCamera.xyz - vWorld);

    PbrSurface surface;
    if (uModes.y > 0.5)
    {
        vec3 specular = uSpecular.rgb;
        float glossiness = uSurface.x;
        if (uFlags.y > 0.5)
        {
            vec4 sg = texture(uSurfaceTexture, vUv);
            specular *= sg.rgb;
            glossiness *= sg.a;
        }
        surface = makeSpecularGlossinessSurface(base.rgb, specular, glossiness, n, v);
    }
    else
    {
        float metallic = uSurface.x;
        float roughness = uSurface.y;
        if (uFlags.y > 0.5)
        {
            vec3 mr = texture(uSurfaceTexture, vUv).rgb;
            roughness *= mr.g;
            metallic *= mr.b;
        }
        surface = makeSurface(base.rgb, metallic, roughness, n, v);
    }

    float occlusion = uFlags.w > 0.5 ? texture(uOcclusionTexture, vUv).r : 1.0;
    vec3 color = evaluateIbl(surface, n, v) * (uExposure.z * occlusion) +
                 evaluateLights(surface, n, v, vWorld, vClip, geometricNormal);
    vec3 emissive = uEmissive.rgb;
    if (uModes.x > 0.5)
        emissive *= texture(uEmissiveTexture, vUv).rgb;
    color += emissive;

    float alpha = uModes.w > 1.5 ? base.a : 1.0;
    color = tonemapFilmic(color * uExposure.x);
    oColor = vec4(pow(color, vec3(1.0 / 2.2)), alpha);
}
