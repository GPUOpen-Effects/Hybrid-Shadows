/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved. 
RTShadowDenoiser - Filter Pass 0
********************************************************************/


struct FFX_DNSR_Shadows_Data_Defn
{
    float4x4 ProjectionInverse;
    int2     BufferDimensions;
    float2   InvBufferDimensions;
    float    DepthSimilaritySigma;
};

cbuffer cbPassData : register(b0)
{
    FFX_DNSR_Shadows_Data_Defn FFX_DNSR_Shadows_Data;
};

Texture2D<float>       t2d_DepthBuffer  : register(t0);
Texture2D<float16_t3>  t2d_NormalBuffer : register(t1);
StructuredBuffer<uint>  sb_tileMetaData : register(t2);

Texture2D<float16_t2>  rqt2d_input  : register(t0, space1);

RWTexture2D<float2> rwt2d_history   : register(u0);
RWTexture2D<unorm float4>  rwt2d_output    : register(u0);

float2 FFX_DNSR_Shadows_GetInvBufferDimensions()
{
    return FFX_DNSR_Shadows_Data.InvBufferDimensions;
}

int2 FFX_DNSR_Shadows_GetBufferDimensions()
{
    return FFX_DNSR_Shadows_Data.BufferDimensions;
}

float4x4 FFX_DNSR_Shadows_GetProjectionInverse()
{
    return FFX_DNSR_Shadows_Data.ProjectionInverse;
}

float FFX_DNSR_Shadows_GetDepthSimilaritySigma()
{
    return FFX_DNSR_Shadows_Data.DepthSimilaritySigma;
}

float FFX_DNSR_Shadows_ReadDepth(int2 p)
{
    return t2d_DepthBuffer.Load(int3(p, 0));
}

float16_t3 FFX_DNSR_Shadows_ReadNormals(int2 p)
{
    return normalize(((float16_t3)t2d_NormalBuffer.Load(int3(p, 0))) * 2 - 1.f);
}

bool FFX_DNSR_Shadows_IsShadowReciever(uint2 p)
{
    float depth = FFX_DNSR_Shadows_ReadDepth(p);
    return (depth > 0.0f) && (depth < 1.0f);
}

float16_t2 FFX_DNSR_Shadows_ReadInput(int2 p)
{
    return (float16_t2)rqt2d_input.Load(int3(p, 0)).xy;
}

uint FFX_DNSR_Shadows_ReadTileMetaData(uint p)
{
    return sb_tileMetaData[p];
}

#include "ffx_shadows_dnsr/ffx_denoiser_shadows_filter.h"

[numthreads(8, 8, 1)]
void Pass0(uint2 gid : SV_GroupID, uint2 gtid : SV_GroupThreadID, uint2 did : SV_DispatchThreadID)
{
    const uint PASS_INDEX = 0;
    const uint STEP_SIZE = 1;

    bool bWriteOutput = false;
    float2 const results = FFX_DNSR_Shadows_FilterSoftShadowsPass(gid, gtid, did, bWriteOutput, PASS_INDEX, STEP_SIZE);

    if (bWriteOutput)
    {
        rwt2d_history[did] = results;
    }
}

[numthreads(8, 8, 1)]
void Pass1(uint2 gid : SV_GroupID, uint2 gtid : SV_GroupThreadID, uint2 did : SV_DispatchThreadID)
{
    const uint PASS_INDEX = 1;
    const uint STEP_SIZE = 2;

    bool bWriteOutput = false;
    float2 const results = FFX_DNSR_Shadows_FilterSoftShadowsPass(gid, gtid, did, bWriteOutput, PASS_INDEX, STEP_SIZE);
    if (bWriteOutput)
    {
        rwt2d_history[did] = results;
    }
}


float ShadowContrastRemapping(float x)
{
    const float a = 10.f;
    const float b = -1.0f;
    const float c = 1 / pow(2, a);
    const float d = exp(-b);
    const float e = 1 / (1 / pow((1 + d), a) - c);
    const float m = 1 / pow((1 + pow(d, x)), a) - c;

    return m * e;
}

[numthreads(8, 8, 1)]
void Pass2(uint2 gid : SV_GroupID, uint2 gtid : SV_GroupThreadID, uint2 did : SV_DispatchThreadID)
{
    const uint PASS_INDEX = 2;
    const uint STEP_SIZE = 4;

    bool bWriteOutput = false;
    float2 const results = FFX_DNSR_Shadows_FilterSoftShadowsPass(gid, gtid, did, bWriteOutput, PASS_INDEX, STEP_SIZE);

    // Recover some of the contrast lost during denoising
    const float shadow_remap = max(1.2f - results.y, 1.0f);
    const float mean = pow(abs(results.x), shadow_remap);

    if (bWriteOutput)
    {
        rwt2d_output[did].x = mean;
    }
}
