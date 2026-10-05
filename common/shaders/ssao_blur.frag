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

layout(set = 0, binding = 0, std140) uniform Blur
{
    vec4 uAxis;
    vec4 uBlur;
    vec4 uKernel[4];
};

layout(set = 1, binding = 0) uniform sampler2D uSsao;

layout(location = 0) out vec4 oColor;

float interleavedGradientNoise(vec2 w)
{
    const vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(w, m.xy)));
}

float unpack(vec2 depth)
{
    return depth.x * (256.0 / 257.0) + depth.y * (1.0 / 257.0);
}

float bilateralWeight(float depth, float sampleDepth)
{
    float diff = (sampleDepth - depth) * uBlur.y;
    return max(0.0, 1.0 - diff * diff);
}

float kernelAt(int i)
{
    return uKernel[i >> 2][i & 3];
}

void tap(inout float sum, inout float totalWeight, float weight, float depth, vec2 position)
{
    vec3 data = textureLod(uSsao, position, 0.0).rgb;
    float bilateral = weight * bilateralWeight(depth, unpack(data.gb));
    sum += data.r * bilateral;
    totalWeight += bilateral;
}

void main()
{
    vec2 uv = vUv;
    vec3 data = textureLod(uSsao, uv, 0.0).rgb;
    if (data.g * data.b == 1.0)
    {
        oColor = vec4(data, 1.0);
        return;
    }

    float depth = unpack(data.gb);
    float totalWeight = kernelAt(0);
    float sum = data.r * totalWeight;

    vec2 offset = uAxis.xy;
    for (int i = 1; i < int(uBlur.x); i++)
    {
        float weight = kernelAt(i);
        tap(sum, totalWeight, weight, depth, uv + offset);
        tap(sum, totalWeight, weight, depth, uv - offset);
        offset += uAxis.xy;
    }

    float ao = sum * (1.0 / totalWeight);
    ao += ((interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0);

    oColor = vec4(ao, data.gb, 1.0);
}
