#version 450
/*
 * Adapted from filament/src/materials/ssao (Apache License 2.0).
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

layout(location = 0) in vec2 vUv;

layout(set = 0, binding = 0, std140) uniform Sao
{
    vec4 uDepthParams;
    vec4 uResolution;
    vec4 uPosition;
    vec4 uSao0;
    vec4 uSao1;
    vec4 uSao2;
    vec4 uSao3;
};

layout(set = 1, binding = 0) uniform sampler2D uDepth0;
layout(set = 1, binding = 1) uniform sampler2D uDepth1;
layout(set = 1, binding = 2) uniform sampler2D uDepth2;
layout(set = 1, binding = 3) uniform sampler2D uDepth3;
layout(set = 1, binding = 4) uniform sampler2D uDepth4;

layout(location = 0) out vec4 oColor;

const float kPi = 3.14159265358979;
const float kLog2LodRate = 3.0;

float sq(float x)
{
    return x * x;
}

float interleavedGradientNoise(vec2 w)
{
    const vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(w, m.xy)));
}

vec2 pack(float normalizedDepth)
{
    float z = clamp(normalizedDepth, 0.0, 1.0);
    float t = floor(256.0 * z);
    float hi = t * (1.0 / 256.0);
    float lo = (256.0 * z) - t;
    return vec2(hi, lo);
}

float linearizeDepth(float depth)
{
    const float preventDiv0 = 1.0 / 16777216.0;
    return (depth * uDepthParams.x + uDepthParams.y) / max(depth * uDepthParams.z + uDepthParams.w, preventDiv0);
}

float sampleDepth(vec2 uv, float lod)
{
    int level = int(lod);
    if (level == 0) return textureLod(uDepth0, uv, 0.0).r;
    if (level == 1) return textureLod(uDepth1, uv, 0.0).r;
    if (level == 2) return textureLod(uDepth2, uv, 0.0).r;
    if (level == 3) return textureLod(uDepth3, uv, 0.0).r;
    return textureLod(uDepth4, uv, 0.0).r;
}

float sampleDepthLinear(vec2 uv, float lod)
{
    return linearizeDepth(sampleDepth(uv, lod));
}

vec3 computeViewSpacePositionFromDepth(vec2 uv, float linearDepth, vec2 positionParams)
{
    return vec3((0.5 - uv) * positionParams * linearDepth, linearDepth);
}

vec3 faceNormal(vec3 dpdx, vec3 dpdy)
{
    return normalize(cross(dpdx, dpdy));
}

vec3 computeViewSpaceNormal(vec2 uv, vec3 position, vec2 texel, vec2 positionParams)
{
    vec2 uvdx = uv + vec2(texel.x, 0.0);
    vec2 uvdy = uv + vec2(0.0, texel.y);
    vec3 px = computeViewSpacePositionFromDepth(uvdx, sampleDepthLinear(uvdx, 0.0), positionParams);
    vec3 py = computeViewSpacePositionFromDepth(uvdy, sampleDepthLinear(uvdy, 0.0), positionParams);
    return faceNormal(px - position, py - position);
}

vec3 tapLocationFast(float i, vec2 p, float noise)
{
    float radius = (i + noise + 0.5) * uSao2.y;
    return vec3(p, radius * radius);
}

void computeAmbientOcclusionSao(inout float occlusion, float i, float ssDiskRadius, vec2 uv,
        vec3 origin, vec3 normal, vec2 tapPosition, float noise)
{
    vec3 tap = tapLocationFast(i, tapPosition, noise);

    float ssRadius = max(1.0, tap.z * ssDiskRadius);

    vec2 uvSamplePos = uv + vec2(ssRadius * tap.xy) * uResolution.zw;

    float level = clamp(floor(log2(ssRadius)) - kLog2LodRate, 0.0, uSao3.x);
    float occlusionDepth = sampleDepthLinear(uvSamplePos, level);
    vec3 p = computeViewSpacePositionFromDepth(uvSamplePos, occlusionDepth, uPosition.xy);

    vec3 v = p - origin;
    float vv = dot(v, v);
    float vn = dot(v, normal);

    float w = sq(max(0.0, 1.0 - vv * uPosition.z));
    w *= step(vv * uPosition.w, vn * vn);

    float sampleOcclusion = max(0.0, vn + (origin.z * uSao0.w)) / (vv + uSao0.x);
    occlusion += w * sampleOcclusion;
}

void main()
{
    vec2 uv = vUv;
    float depth = sampleDepth(uv, 0.0);
    if (depth >= 1.0)
    {
        oColor = vec4(1.0);
        return;
    }
    float z = linearizeDepth(depth);
    vec3 origin = computeViewSpacePositionFromDepth(uv, z, uPosition.xy);
    vec3 normal = computeViewSpaceNormal(uv, origin, uResolution.zw, uPosition.xy);

    float occlusion = 0.0;
    if (uSao1.y > 0.0)
    {
        float noise = interleavedGradientNoise(gl_FragCoord.xy);
        float angle0 = ((2.0 * kPi) * 2.4) * noise;
        vec2 tapPosition = vec2(cos(angle0), sin(angle0));
        mat2 angleStep = mat2(uSao2.z, uSao2.w, -uSao2.w, uSao2.z);
        float ssDiskRadius = -(uSao0.z / origin.z);
        for (float i = 0.0; i < uSao2.x; i += 1.0)
        {
            computeAmbientOcclusionSao(occlusion, i, ssDiskRadius, uv, origin, normal, tapPosition, noise);
            tapPosition = angleStep * tapPosition;
        }
        occlusion = sqrt(occlusion * uSao1.y);
    }

    float aoVisibility = pow(clamp(1.0 - occlusion, 0.0, 1.0), uSao1.x);
    oColor = vec4(aoVisibility, pack(origin.z * uSao1.w), 1.0);
}
