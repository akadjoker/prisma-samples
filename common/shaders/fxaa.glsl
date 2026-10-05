#ifndef FXAA_GLSL
#define FXAA_GLSL

/**
  G3D version of FXAA. See copyright and warranty statement below.

  G3D Innovation Engine http://casual-effects.com/g3d
  Copyright 2000-2018, Morgan McGuire
  All rights reserved
  Available under the BSD License
*/

/*============================================================================


                    NVIDIA FXAA 3.11 by TIMOTHY LOTTES
               (modified for G3D with bug fixes and PC GLSL preamble)

------------------------------------------------------------------------------
COPYRIGHT (C) 2010, 2011 NVIDIA CORPORATION. ALL RIGHTS RESERVED.
------------------------------------------------------------------------------
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED
*AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS
OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL NVIDIA
OR ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR
LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION,
OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE
THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
DAMAGES.
============================================================================*/

// Includers declare `uniform sampler2D uColor` (rgb in the perceptual space, luma in alpha).

const float kFxaaMediumpFltMin = 0.00006103515625;

vec4 fxaa(vec2 pos, vec4 posPos, vec2 rcpFrameOpt, vec2 rcpFrameOpt2, float edgeSharpness,
        float edgeThreshold, float edgeThresholdMin)
{
    float lumaNw = textureLod(uColor, posPos.xy, 0.0).w;
    float lumaSw = textureLod(uColor, posPos.xw, 0.0).w;
    float lumaNe = textureLod(uColor, posPos.zy, 0.0).w;
    float lumaSe = textureLod(uColor, posPos.zw, 0.0).w;

    vec4 rgbyM = textureLod(uColor, pos.xy, 0.0);
    float lumaM = rgbyM.w;

    float lumaMaxNwSw = max(lumaNw, lumaSw);
    float lumaMinNwSw = min(lumaNw, lumaSw);
    float lumaMaxNeSe = max(lumaNe, lumaSe);
    float lumaMinNeSe = min(lumaNe, lumaSe);
    float lumaMax = max(lumaMaxNeSe, lumaMaxNwSw);
    float lumaMin = min(lumaMinNeSe, lumaMinNwSw);

    float lumaMaxScaled = lumaMax * edgeThreshold;
    float lumaMinM = min(lumaMin, lumaM);
    float lumaMaxScaledClamped = max(edgeThresholdMin, lumaMaxScaled);
    float lumaMaxM = max(lumaMax, lumaM);
    float lumaMaxSubMinM = lumaMaxM - lumaMinM;

    if (lumaMaxSubMinM < lumaMaxScaledClamped) return rgbyM;

    float dirSwMinusNe = lumaSw - lumaNe;
    float dirSeMinusNw = lumaSe - lumaNw;
    vec2 dir = vec2(dirSwMinusNe + dirSeMinusNw, dirSwMinusNe - dirSeMinusNw);

    float dirLength = length(dir.xy);
    if (dirLength < kFxaaMediumpFltMin) return rgbyM;

    vec2 dir1 = dir.xy / dirLength;

    vec4 rgbyN1 = textureLod(uColor, pos.xy - dir1 * rcpFrameOpt.xy, 0.0);
    vec4 rgbyP1 = textureLod(uColor, pos.xy + dir1 * rcpFrameOpt.xy, 0.0);

    float dirAbsMinTimesC = max(abs(dir1.x), abs(dir1.y)) * edgeSharpness * 0.015;
    vec2 dir2 = dir1.xy * min(lumaMaxSubMinM / dirAbsMinTimesC, 3.0);
    vec4 rgbyN2 = textureLod(uColor, pos.xy - dir2 * rcpFrameOpt2.xy, 0.0);
    vec4 rgbyP2 = textureLod(uColor, pos.xy + dir2 * rcpFrameOpt2.xy, 0.0);

    vec4 rgbyA = rgbyN1 + rgbyP1;
    vec4 rgbyB = ((rgbyN2 + rgbyP2) * 0.25) + (rgbyA * 0.25);

    bool twoTap = (rgbyB.w < lumaMin) || (rgbyB.w > lumaMax);
    if (twoTap) rgbyB.xyz = rgbyA.xyz * 0.5;

    return mix(rgbyB, rgbyM, 0.25);
}

#endif
