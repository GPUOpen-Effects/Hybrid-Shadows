/**********************************************************************
Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
RTShadowDenoiser

tile_classification

********************************************************************/

struct FFX_DNSR_Shadows_Data_Defn
{
    float3   Eye;
    int      FirstFrame;
    int2     BufferDimensions;
    float2   InvBufferDimensions;
    float4x4 ProjectionInverse;
    float4x4 ReprojectionMatrix;
    float4x4 ViewProjectionInverse;
};

cbuffer cbPassData : register(b0)
{
    FFX_DNSR_Shadows_Data_Defn FFX_DNSR_Shadows_Data;
}

Texture2D<float>            t2d_depth              : register(t0);
Texture2D<float2>           t2d_velocity           : register(t1);
Texture2D<float3>           t2d_normal             : register(t2);
Texture2D<float2>           t2d_history            : register(t3);
Texture2D<float>            t2d_previousDepth      : register(t4);
StructuredBuffer<uint>      sb_raytracerResult     : register(t5);


RWStructuredBuffer<uint>    rwsb_tileMetaData             : register(u0);
RWTexture2D<float2>         rwt2d_reprojectionResults     : register(u1); 

Texture2D<float3>           t2d_previousMoments    : register(t0, space1);
RWTexture2D<float3>         rwt2d_momentsBuffer           : register(u0, space1); 

SamplerState ss_trilinerClamp : register(s0);

float4x4 FFX_DNSR_Shadows_GetViewProjectionInverse()
{
    return FFX_DNSR_Shadows_Data.ViewProjectionInverse;
}

float4x4 FFX_DNSR_Shadows_GetReprojectionMatrix()
{
    return FFX_DNSR_Shadows_Data.ReprojectionMatrix;
}

float4x4 FFX_DNSR_Shadows_GetProjectionInverse()
{
    return FFX_DNSR_Shadows_Data.ProjectionInverse;
}

float2 FFX_DNSR_Shadows_GetInvBufferDimensions()
{
    return FFX_DNSR_Shadows_Data.InvBufferDimensions;
}

int2 FFX_DNSR_Shadows_GetBufferDimensions()
{
    return FFX_DNSR_Shadows_Data.BufferDimensions;
}

int FFX_DNSR_Shadows_IsFirstFrame()
{
    return FFX_DNSR_Shadows_Data.FirstFrame;
}

float3 FFX_DNSR_Shadows_GetEye()
{
    return FFX_DNSR_Shadows_Data.Eye;
}

float FFX_DNSR_Shadows_ReadDepth(int2 p)
{
    return t2d_depth.Load(int3(p, 0)).x;
}

float FFX_DNSR_Shadows_ReadPreviousDepth(int2 p)
{
    return t2d_previousDepth.Load(int3(p, 0)).x;
} 

float3 FFX_DNSR_Shadows_ReadNormals(int2 p)
{
    return normalize(t2d_normal.Load(int3(p, 0)).xyz * 2 - 1.f);
}

float2 FFX_DNSR_Shadows_ReadVelocity(int2 p)
{
    return t2d_velocity.Load(int3(p, 0));
}

float FFX_DNSR_Shadows_ReadHistory(float2 p)
{
    return t2d_history.SampleLevel(ss_trilinerClamp, p, 0).x;
}

float3 FFX_DNSR_Shadows_ReadPreviousMomentsBuffer(int2 p)
{
    return t2d_previousMoments.Load(int3(p, 0)).xyz;
}

uint  FFX_DNSR_Shadows_ReadRaytracedShadowMask(uint p)
{
    return sb_raytracerResult[p];
}

void  FFX_DNSR_Shadows_WriteMetadata(uint p, uint val)
{
    rwsb_tileMetaData[p] = val;
}

void  FFX_DNSR_Shadows_WriteMoments(uint2 p, float3 val)
{
    rwt2d_momentsBuffer[p] = val;
}

void FFX_DNSR_Shadows_WriteReprojectionResults(uint2 p, float2 val)
{
    rwt2d_reprojectionResults[p] = val;
}

bool FFX_DNSR_Shadows_IsShadowReciever(uint2 p)
{
    float depth = FFX_DNSR_Shadows_ReadDepth(p);
    return (depth > 0.0f) && (depth < 1.0f);
}

#include "ffx_shadows_dnsr/ffx_denoiser_shadows_tileclassification.h"

[numthreads(8, 8, 1)]
void main(uint group_index : SV_GroupIndex, uint2 gid : SV_GroupID)
{
    FFX_DNSR_Shadows_TileClassification(group_index, gid);
}
