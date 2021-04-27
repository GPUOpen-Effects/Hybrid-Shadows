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

#include "RaytracingCommon.h"
#include "Utilities.h"

//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------

struct ClassifyResults
{
	bool bIsActiveLane;
	bool bIsInLight;
	float minT;
	float maxT;
};

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
Texture2D<float>  t2d_depth		: register(t0);
Texture2D<float3> t2d_normals	: register(t1);
Texture2DArray<float>  t2d_shadowMap : register(t2);

// using uint4 so we can pack the tile ourselves
RWStructuredBuffer<uint4> rwsb_tiles : register(u0);
globallycoherent RWBuffer<uint> rwb_tileCount : register(u1);

RWTexture2D<uint> rwt2d_rayHitResults : register(u2);

SamplerState ss_point : register(s0);
SamplerComparisonState scs_shadows : register(s1);

//--------------------------------------------------------------------------------------
// Main function
//--------------------------------------------------------------------------------------

void WriteTile(Tile const currentTile)
{
	uint index = ~0;
	InterlockedAdd(rwb_tileCount[0], 1, index);
	rwsb_tiles[index] = Tile::ToUint(currentTile);
}

ClassifyResults Classify(
	uint2 const pixelCoord,
	bool const bUseNormal,
	bool const bUseCascadeSplits,
	bool const bUseCascadeBlocking)
{
	bool const bIsInViewport = all(pixelCoord.xy < textureSize.xy);

	float const depth = t2d_depth[pixelCoord];

	bool bIsActiveLane = bIsInViewport && (depth < 1.0f);
	bool bIsInLight = false;
	float minT = 1.#INF;
	float maxT = 0.f;

	if (bUseNormal && bIsActiveLane)
	{
		float3 const normal = normalize(t2d_normals[pixelCoord].xyz * 2 - 1.f);
		bool const bIsNormalFacingLight = dot(normal, -lightDir) > 0;

		bIsActiveLane = bIsActiveLane && bIsNormalFacingLight;
	}

	if ((bUseCascadeSplits || bUseCascadeBlocking) && bIsActiveLane)
	{
		float2 const uv = pixelCoord * textureSize.zw;
		float4 const homogeneous = mul(viewToWorld, float4(2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f, depth, 1));
		float3 const worldPos = homogeneous.xyz / homogeneous.w;

		float3 const lightViewSpacePos = mul(lightView, float4(worldPos, 1)).xyz;

		bool bIsInActiveCascade = false;
		if (bUseCascadeSplits)
		{
			for (uint i = 0; i < cascadeCount; ++i)
			{
				uint const cascadeMask = 1 << i;

				float3 const shadowCoord = lightViewSpacePos * cascadeScale[i].xyz + cascadeOffset[i].xyz;
				if (min(shadowCoord.x, shadowCoord.y) > 0 && max(shadowCoord.x, shadowCoord.y) < 1)
				{
					bIsInActiveCascade = cascadeMask & activeCascades;
					break;
				}
			}
		}

		if (bUseCascadeBlocking)
		{
			float const radius = sunSizeLightSpace * lightViewSpacePos.z;

			float3 shadowCoord = float3(0, 0, 0);
			uint cascadeIndex = 0;
			for (uint i = 0; i < cascadeCount; ++i)
			{
				shadowCoord = lightViewSpacePos * cascadeScale[i].xyz + cascadeOffset[i].xyz;
				if (all(shadowCoord.xy > 0) && all(shadowCoord.xy < 1))
				{
					cascadeIndex = i;
					break;
				}
			}

			// grow search area by a pixel to make sure we search a wide enough area
			// also scale everything from UV to pixel coord for image loads.
			float2 const radiusCoord = abs(radius * cascadeScale[cascadeIndex].xy) * cascadeSize + 1.xx;
			shadowCoord.xy *= cascadeSize;

			float const depthCmp = shadowCoord.z - blockerOffset;

			float maxD = 0;
			float minD = 1;
			float closetDepth = 0;

			// With small shadow maps we will be bound on filtering since the shadow map can end up completely in LO cache
			// useing an image load is faster then a sample in RDNA but we will be losing the benifit of doing some of the ALU
			// in the filter and getting 4 pixels of data per tap. 
			for (uint x = 0; x < k_poissonDiscSampleCountHigh; ++x)
			{
				float2 const sampleUV = shadowCoord.xy + k_poissonDisc[x] * radiusCoord + 0.5f;
				float const pixelDepth = t2d_shadowMap.Load(uint4(sampleUV, cascadeIndex, 0));

				// using min and max to reduce number of cmps
				maxD = max(maxD, pixelDepth);
				minD = min(minD, pixelDepth);

				// need to fihnd closet point in front of the receiver
				if (pixelDepth < depthCmp)
				{
					closetDepth = max(closetDepth, pixelDepth);
				}
			}

			const bool bIsInShadow = (maxD <= depthCmp);
			bIsInLight = bRejectLitPixels && (minD >= depthCmp);
			bIsInActiveCascade = !bIsInShadow && !bIsInLight;

			if (bIsInActiveCascade && bUseCascadesForRayT)
			{
				float const viewMinT = abs(max(shadowCoord.z - closetDepth - blockerOffset, 0) / cascadeScale[cascadeIndex].z);
				float const viewMaxT = abs((shadowCoord.z - minD + blockerOffset) / cascadeScale[cascadeIndex].z);

				// if its knowen that the light view matrix is only a rotation or has unifrom scale this can be optimized.
				minT = length(mul(inverseLightView, float4(0, 0, viewMinT, 0)).xyz);
				maxT = length(mul(inverseLightView, float4(0, radius, viewMaxT, 0)).xyz);

			}
		}

		bIsActiveLane = bIsActiveLane && bIsInActiveCascade;
	}

	ClassifyResults const results = { bIsActiveLane, bIsInLight, minT, maxT };

	return results;
}

[numthreads(TILE_SIZE_X * TILE_SIZE_Y, 1, 1)]
void ClassifyByNormal(uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint2 const localID = FXX_Rmp8x8(localIndex);
	uint2 const pixelCoord = groupID.xy * k_tileSize + localID.xy;

	ClassifyResults const results =
		Classify(
			pixelCoord,
			true,
			false,
			false);

	Tile currentTile = Tile::Create(groupID.xy);
	uint const mask = BoolToWaveMask(results.bIsActiveLane, localID);
	currentTile.mask = mask;

	uint const lightMask = BoolToWaveMask(results.bIsInLight, localID);

	bool const bDiscardTile = (countbits(mask) <= tileTolerance);
	if (localIndex == 0)
	{
		if (!bDiscardTile)
		{
			WriteTile(currentTile);
		}

		rwt2d_rayHitResults[groupID.xy] = ~lightMask;
	}
}

[numthreads(TILE_SIZE_X * TILE_SIZE_Y, 1, 1)]
void ClassifyByCascadeRange(uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint2 const localID = FXX_Rmp8x8(localIndex);
	uint2 const pixelCoord = groupID.xy * k_tileSize + localID.xy;

	ClassifyResults const results =
		Classify(
			pixelCoord,
			true,
			true,
			false);

	Tile currentTile = Tile::Create(groupID.xy);
	uint const mask = BoolToWaveMask(results.bIsActiveLane, localID);
	currentTile.mask = mask;

	uint const lightMask = BoolToWaveMask(results.bIsInLight, localID);

	bool const bDiscardTile = (countbits(mask) <= tileTolerance);
	if (localIndex == 0)
	{
		if (!bDiscardTile)
		{
			WriteTile(currentTile);
		}

		rwt2d_rayHitResults[groupID.xy] = ~lightMask;
	}
}


[numthreads(TILE_SIZE_X * TILE_SIZE_Y, 1, 1)]
void ClassifyByCascades(uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint2 const localID = FXX_Rmp8x8(localIndex);
	uint2 const pixelCoord = groupID.xy * k_tileSize + localID.xy;

	ClassifyResults const results =
		Classify(
			pixelCoord,
			true,
			false,
			true);

	Tile currentTile = Tile::Create(groupID.xy);
	uint const mask = BoolToWaveMask(results.bIsActiveLane, localID);
	currentTile.mask = mask;
	if (bUseCascadesForRayT)
	{
		// At lest one lane must be active for the tile to be written out, so the infinitly and zero will be removed by the wave min and max.
		// Otherwise we will get minT to be infinite and maxT to be 0
		currentTile.minT = max(WaveActiveMin(results.minT), currentTile.minT);
		currentTile.maxT = min(WaveActiveMax(results.maxT), currentTile.maxT);
	}

	uint const lightMask = BoolToWaveMask(results.bIsInLight, localID);

	bool const bDiscardTile = (countbits(mask) <= tileTolerance);
	if (localIndex == 0)
	{
		if (!bDiscardTile)
		{
			WriteTile(currentTile);
		}

		rwt2d_rayHitResults[groupID.xy] = ~lightMask;

	}
}