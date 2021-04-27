// AMD Cauldron code
// 
// Copyright(c) 2020 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 

#include "Utilities.h"

//--------------------------------------------------------------------------------------
// Constant Buffers
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// PerFrame structure, must match the one in GlTFCommon.h
//--------------------------------------------------------------------------------------

cbuffer cbPerFrame : register(b0)
{
    //// shadow cascades data
    matrix          m_mInverseCameraCurrViewProj;
    matrix          m_mLightView;
    float4          m_vCascadeOffset[4];
    float4          m_vCascadeScale[4];

    uint            m_nCascadeLevels; // Number of Cascades
    uint2           m_nTextureSize;
    float           _pad;

    // For Map based selection scheme, this keeps the pixels inside of the the valid range.
    // When there is no boarder, these values are 0 and 1 respectivley.
    float           m_fMinBorderPadding;
    float           m_fMaxBorderPadding;
    float           m_fShadowBiasFromGUI;  // A shadow map offset to deal with self shadow artifacts.  
                                           //These artifacts are aggravated by PCF.
    float           m_fCascadeBlendArea; // Amount to overlap when blending between cascades.

    float3          m_fLightDir;
    float           m_fSunSize;
};

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
Texture2DArray<float> CascadeBuffer : register(t0);
Texture2D DepthBuffer : register(t1);
RWTexture2D<float4> OutputBuffer : register(u0);

SamplerComparisonState     shadowSampler    : register(s0);
SamplerState               sSampler         : register(s1);

//--------------------------------------------------------------------------------------
// Use PCF to sample the depth map and return a percent lit value.
//--------------------------------------------------------------------------------------
float CalculatePCFPercentLit(in float4 vShadowTexCoord,
    in uint cascadeIndex,
    in float fPenumbraSize)
{
    float2 const radiusCoord = abs(fPenumbraSize * m_vCascadeScale[cascadeIndex].xy);

    float const depthcompare = vShadowTexCoord.z - m_fShadowBiasFromGUI;
    // A very simple solution to the depth bias problems of PCF is to use an offset.
    // Unfortunately, too much offset can lead to Peter-panning (shadows near the base of object disappear )
    // Too little offset can lead to shadow acne ( objects that should not be in shadow are partially self shadowed ).

    float fPercentLit = 0.0f;
    for (uint x = 0; x < k_poissonDiscSampleCountMid; ++x)
    {
        float2 const sampleUV = vShadowTexCoord.xy + k_poissonDisc[x] * radiusCoord;
        fPercentLit += CascadeBuffer.SampleCmpLevelZero(shadowSampler, float3(sampleUV, cascadeIndex), depthcompare);
    }

    return fPercentLit / k_poissonDiscSampleCountMid;
}

float GetPenumbraSize(in float4 vShadowMapTextureCoord, in float radius, in int cascadeIndex)
{
    float2 const radiusCoord = abs(radius * m_vCascadeScale[cascadeIndex].xy);

    int blockerCount = 0;
    float totalShadowDepth = 0.0f;

    for (uint x = 0; x < k_poissonDiscSampleCountMid; ++x)
    {
        float2 const sampleUV = vShadowMapTextureCoord.xy + k_poissonDisc[x] * radiusCoord;
        float const shadowDepth = CascadeBuffer.SampleLevel(sSampler, float3(sampleUV, cascadeIndex), 0);

        if (shadowDepth < vShadowMapTextureCoord.z - m_fShadowBiasFromGUI)
        {
            totalShadowDepth += shadowDepth;
            blockerCount += 1;
        }
    }

    float const averageBlockerDepth = totalShadowDepth / blockerCount;
    float const penumbraSize = (blockerCount > 0) ? (vShadowMapTextureCoord.z - averageBlockerDepth) : 0;
    return abs(radius * (1 - penumbraSize));
}

//--------------------------------------------------------------------------------------
// Calculate amount to blend between two cascades and the band where blending will occure.
//--------------------------------------------------------------------------------------
void CalculateBlendAmountForMap(in float4 vShadowMapTextureCoord,
    in out float fCurrentPixelsBlendBandLocation,
    out float fBlendBetweenCascadesAmount)
{
    // Calcaulte the blend band for the map based selection.
    float2 distanceToOne = float2 (1.0f - vShadowMapTextureCoord.x, 1.0f - vShadowMapTextureCoord.y);
    fCurrentPixelsBlendBandLocation = min(vShadowMapTextureCoord.x, vShadowMapTextureCoord.y);
    float fCurrentPixelsBlendBandLocation2 = min(distanceToOne.x, distanceToOne.y);
    fCurrentPixelsBlendBandLocation =
        min(fCurrentPixelsBlendBandLocation, fCurrentPixelsBlendBandLocation2);
    fBlendBetweenCascadesAmount = fCurrentPixelsBlendBandLocation / m_fCascadeBlendArea;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void mainCS(uint3 Tid : SV_DispatchThreadID)
{
    float4 vShadowMapTextureCoord = 0.0f;
    float4 vShadowMapTextureCoord_blend = 0.0f;

    float fPercentLit = 1.0f;

    int iNextCascadeIndex = 1;

    // This for loop is not necessary when the frustum is uniformaly divided and interval based selection is used.
    // In this case fCurrentPixelDepth could be used as an array lookup into the correct frustum. 
    int iCurrentCascadeIndex = 0;


    // Retrieve the iterated texel
    if (any(Tid.xy >= m_nTextureSize)) return;    // out of bounds
    const float2 uv = (Tid.xy + 0.5f) / float2(m_nTextureSize);
    const float depth = DepthBuffer.Load(int3(Tid.xy, 0)).x;
    const float2 ndc = 2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f;

    // No need to evaluate sky pixels
    if (depth < 1.0f)
    {
        // Recover the world-space position
        const float4 homogeneous = mul(m_mInverseCameraCurrViewProj, float4(ndc, depth, 1.0f));
        float3 world_position;
        world_position.xyz = homogeneous.xyz / homogeneous.w;  // perspective divide 

        float4 const vShadowMapTextureCoordViewSpace = mul(m_mLightView, float4(world_position, 1));
        for (int iCascadeIndex = 0; iCascadeIndex < m_nCascadeLevels; ++iCascadeIndex)
        {
            vShadowMapTextureCoord = vShadowMapTextureCoordViewSpace * m_vCascadeScale[iCascadeIndex];
            vShadowMapTextureCoord += m_vCascadeOffset[iCascadeIndex];

            if (min(vShadowMapTextureCoord.x, vShadowMapTextureCoord.y) > m_fMinBorderPadding
                && max(vShadowMapTextureCoord.x, vShadowMapTextureCoord.y) < m_fMaxBorderPadding)
            {
                iCurrentCascadeIndex = iCascadeIndex;
                break;
            }
        }

        // Repeat text coord calculations for the next cascade. 
        // The next cascade index is used for blurring between maps.
        iNextCascadeIndex = min(m_nCascadeLevels - 1, iCurrentCascadeIndex + 1);

        float fBlendBetweenCascadesAmount = 1.0f;
        float fCurrentPixelsBlendBandLocation = 1.0f;

        CalculateBlendAmountForMap(vShadowMapTextureCoord,
            fCurrentPixelsBlendBandLocation, fBlendBetweenCascadesAmount);

        float3 const coneVec = normalize(-m_fLightDir) + CreateTangentVectors(-m_fLightDir)[0] * m_fSunSize;
        float3 const lightSpaceConeVec = mul(m_mLightView, float4(coneVec, 0)).xyz;
        float const radius = length(lightSpaceConeVec.xy) / lightSpaceConeVec.z * vShadowMapTextureCoordViewSpace.z;

        float penumbraSize = GetPenumbraSize(vShadowMapTextureCoord, radius, iCurrentCascadeIndex);
       
        if (penumbraSize > 0.0f)
        {
            OutputBuffer[Tid.xy] = 1.f;

            fPercentLit = CalculatePCFPercentLit(vShadowMapTextureCoord, iCurrentCascadeIndex, penumbraSize);

            if (m_nCascadeLevels > 1)
            {
                if (fCurrentPixelsBlendBandLocation < m_fCascadeBlendArea)
                {  // the current pixel is within the blend band.

                    // Repeat text coord calculations for the next cascade. 
                    // The next cascade index is used for blurring between maps.
                    vShadowMapTextureCoord_blend = vShadowMapTextureCoordViewSpace * m_vCascadeScale[iNextCascadeIndex];
                    vShadowMapTextureCoord_blend += m_vCascadeOffset[iNextCascadeIndex];

                    penumbraSize = GetPenumbraSize(vShadowMapTextureCoord_blend, radius, iNextCascadeIndex);
                    if (penumbraSize > 0.0f)
                    {
                        float const fPercentLit_blend = CalculatePCFPercentLit(vShadowMapTextureCoord_blend, iNextCascadeIndex, penumbraSize);
                        fPercentLit = lerp(fPercentLit_blend, fPercentLit, fBlendBetweenCascadesAmount);
                        // Blend the two calculated shadows by the blend amount.
                    }
                }
            }
        }
    }

    // Write the results out to memory
    OutputBuffer[Tid.xy] = fPercentLit;
}
