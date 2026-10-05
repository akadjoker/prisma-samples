#version 450

/*
 * Adapted from filament/src/materials/separableGaussianBlur.fs (Apache License 2.0).
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
    vec4 uKernel[6];
};

layout(set = 1, binding = 0) uniform sampler2D uSource;

layout(location = 0) out vec4 oColor;

float vmax(vec4 v)
{
    return max(v.x, max(v.y, v.z));
}

void main()
{
    vec2 axis = uAxis.xy;
    bool reinhard = uAxis.z > 0.5;
    int count = int(uAxis.w);

    vec4 sum = vec4(0.0);
    if (reinhard)
    {
        float totalWeight = 0.0;
        vec4 s = textureLod(uSource, vUv, 0.0);
        float w = uKernel[0].x / (1.0 + vmax(s));
        totalWeight += w;
        sum += s * w;
        vec2 offset = axis;
        for (int i = 1; i < count; i++, offset += axis * 2.0)
        {
            float k = uKernel[i].x;
            vec2 o = offset + axis * uKernel[i].y;
            vec4 s0 = textureLod(uSource, vUv + o, 0.0);
            vec4 s1 = textureLod(uSource, vUv - o, 0.0);
            float w0 = k / (1.0 + vmax(s0));
            float w1 = k / (1.0 + vmax(s1));
            totalWeight += w0 + w1;
            sum += s0 * w0 + s1 * w1;
        }
        sum *= 1.0 / totalWeight;
    }
    else
    {
        sum += textureLod(uSource, vUv, 0.0) * uKernel[0].x;
        vec2 offset = axis;
        for (int i = 1; i < count; i++, offset += axis * 2.0)
        {
            float k = uKernel[i].x;
            vec2 o = offset + axis * uKernel[i].y;
            sum += textureLod(uSource, vUv + o, 0.0) * k;
            sum += textureLod(uSource, vUv - o, 0.0) * k;
        }
    }
    oColor = vec4(sum.rgb, 1.0);
}
