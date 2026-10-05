#ifndef LTC_GLSL
#define LTC_GLSL

// Rectangular area lights with linearly transformed cosines (Heitz, Dupuy, Hill and Neubelt,
// "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines", SIGGRAPH 2016): the
// GGX lobe is fitted by a clamped cosine under a 3x3 matrix, so the integral of the BRDF over the
// light is the integral of a clamped cosine over the light polygon transformed by the inverse of
// that matrix, which has a closed form for the edges of a polygon.

#include "pbr_surface.glsl"

// The inverse matrices of LtcTables.h, 64 x 64 texels of RGBA32F (or RGBA16F), sampled with
// linear filtering and clamping.
layout(set = 1, binding = 2) uniform sampler2D uLtcInverse;

const float kLtcSize = 64.0;

struct AreaLight
{
    // the corners in world space, counter clockwise as seen from the side the light shines on
    vec4 corners[4];
    vec4 colorIntensity;
    // x: 1 when the light shines on both sides
    vec4 flags;
};

mat3 ltcInverse(float perceptualRoughness, float noV)
{
    vec2 uv = vec2(perceptualRoughness, sqrt(1.0 - clamp(noV, 0.0, 1.0)));
    uv = uv * ((kLtcSize - 1.0) / kLtcSize) + 0.5 / kLtcSize;
    vec4 t = textureLod(uLtcInverse, uv, 0.0);
    return mat3(vec3(t.x, 0.0, t.y), vec3(0.0, 1.0, 0.0), vec3(t.z, 0.0, t.w));
}

// The integral of the clamped cosine over the edge from v1 to v2 (unit vectors), as the vector
// whose z is the contribution to the form factor.
vec3 ltcIntegrateEdge(vec3 v1, vec3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaOverSin = x > 0.0 ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * thetaOverSin;
}

// The part of the quad above the horizon z = 0 as up to five vertices; returns their number.
int ltcClipToHorizon(vec3 quad[4], out vec3 clipped[5])
{
    int count = 0;
    for (int i = 0; i < 4; ++i)
    {
        vec3 a = quad[i];
        vec3 b = quad[(i + 1) & 3];
        if (a.z >= 0.0)
            clipped[count++] = a;
        if ((a.z >= 0.0) != (b.z >= 0.0))
            clipped[count++] = a + (b - a) * (a.z / (a.z - b.z));
    }
    return count;
}

// The integral of the clamped cosine distribution under the inverse matrix over the quad (corners
// relative to the shaded point, in the frame where the matrix applies): the fraction of the light
// of a surface that sees the whole hemisphere, from 0 to 1.
float ltcPolygon(mat3 inverseMatrix, vec3 n, vec3 v, vec3 corners[4], bool twoSided)
{
    vec3 t1 = v - n * dot(v, n);
    float length1 = length(t1);
    t1 = length1 > 1e-5 ? t1 / length1 : normalize(abs(n.x) > 0.9 ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
    vec3 t2 = cross(n, t1);
    mat3 toLocal = inverseMatrix * transpose(mat3(t1, t2, n));

    vec3 quad[4];
    for (int i = 0; i < 4; ++i)
        quad[i] = toLocal * corners[i];

    vec3 clipped[5];
    int count = ltcClipToHorizon(quad, clipped);
    if (count < 3)
        return 0.0;
    for (int i = 0; i < 5; ++i)
        if (i < count)
            clipped[i] = normalize(clipped[i]);

    float sum = 0.0;
    for (int i = 0; i < 5; ++i)
        if (i < count)
            sum += ltcIntegrateEdge(clipped[i], clipped[i + 1 < count ? i + 1 : 0]).z;
    // the edges of a polygon that faces the point run clockwise in this frame
    return twoSided ? abs(sum) : max(-sum, 0.0);
}

// The light reflected by the surface towards v from one area light: the diffuse part is the
// integral of a plain clamped cosine, the specular part the one of the cosine fitted to the GGX
// lobe, scaled by the split sum terms of the surface.
vec3 areaLightShading(PbrSurface surface, AreaLight light, vec3 n, vec3 v, vec3 worldPosition)
{
    vec3 corners[4];
    for (int i = 0; i < 4; ++i)
        corners[i] = light.corners[i].xyz - worldPosition;

    vec3 lightNormal = cross(corners[1] - corners[0], corners[3] - corners[0]);
    bool twoSided = light.flags.x > 0.5;
    if (!twoSided && dot(lightNormal, corners[0]) > 0.0)
        return vec3(0.0);

    vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.w;
    float diffuse = ltcPolygon(mat3(1.0), n, v, corners, twoSided);
    mat3 inverseMatrix = ltcInverse(surface.perceptualRoughness, surface.noV);
    float specular = ltcPolygon(inverseMatrix, n, v, corners, twoSided);

    vec3 e = mix(surface.dfg.xxx, surface.dfg.yyy, surface.f0);
    vec3 color = surface.diffuseColor * (1.0 - e) * diffuse +
                 e * surface.energyCompensation * specular;
    return color * radiance;
}

#endif
