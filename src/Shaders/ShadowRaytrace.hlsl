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

static const RAY_FLAG k_opaqueFlags
= RAY_FLAG_FORCE_OPAQUE
| RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
| RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
| RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

static const RAY_FLAG k_nonOpaqueFlags
= RAY_FLAG_FORCE_NON_OPAQUE
| RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
| RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
| RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

static const RAY_FLAG k_mixedFlags
= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
| RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
| RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
Texture2D<float>  t2d_depth		: register(t0);
Texture2D<float3> t2d_normals	: register(t1);
Texture2D         t2d_blueNoise : register(t2);

StructuredBuffer<uint4> sb_tiles  : register(t3);
StructuredBuffer<UV> sb_uvBuffer : register(t4);

RaytracingAccelerationStructure ras_opaque : register(t0, space1);
RaytracingAccelerationStructure ras_nonOpaque : register(t1, space1);

Texture2D t2d_maskTextures[] : register(t0, space2);

RWTexture2D<uint> rwt2d_rayHitResults : register(u0);

SamplerState ss_mask : register(s0);

//--------------------------------------------------------------------------------------
// Main function
//--------------------------------------------------------------------------------------

bool CheckAlphaMask(uint primIndex, uint textureIndex, float2 barycentrics, float t)
{
	UV const packedUVs = sb_uvBuffer[primIndex];
	float2 const uv = packedUVs.uv0 + packedUVs.uv01 * barycentrics.x + packedUVs.uv02 * barycentrics.y;

	//float const coneR = t * tanOfConeAngle;
	//float2 const grad0 = coneR * packedUVs.uv01;
	//float2 const grad1 = coneR * packedUVs.uv02;

	Texture2D mask = t2d_maskTextures[NonUniformResourceIndex(textureIndex)];
	//float const alpha = mask.SampleGrad(ss_mask, uv, grad0, grad1).a;
	float const alpha = mask.SampleLevel(ss_mask, uv, 0).a;

	return (alpha > 0.5);
}

bool TraceOpaque(RaytracingAccelerationStructure ras, RayDesc ray)
{
	RayQuery<k_opaqueFlags> q;

	q.TraceRayInline(
		ras,
		k_opaqueFlags,
		0xff,
		ray);

	q.Proceed();

	return q.CommittedStatus() != COMMITTED_NOTHING;
}

bool TraceNonOpaque(RaytracingAccelerationStructure ras, RayDesc ray)
{
	RayQuery<k_nonOpaqueFlags> q;

	q.TraceRayInline(
		ras,
		k_nonOpaqueFlags,
		0xff,
		ray);

	while (q.Proceed())
	{
		uint const uvOffset = q.CandidateInstanceID();
		uint const primOffset = q.CandidatePrimitiveIndex();
		uint const textureIndex = q.CandidateInstanceContributionToHitGroupIndex();
		float2 const barycentrics = q.CandidateTriangleBarycentrics();
		float const t = q.CandidateTriangleRayT();

		if (CheckAlphaMask(uvOffset + primOffset, textureIndex, barycentrics, t))
		{
			q.CommitNonOpaqueTriangleHit();
		}
	}

	return q.CommittedStatus() != COMMITTED_NOTHING;
}

bool TraceMixed(RaytracingAccelerationStructure ras, RayDesc ray)
{
	RayQuery<k_mixedFlags> q;

	q.TraceRayInline(
		ras,
		k_mixedFlags,
		0xff,
		ray);

	while (q.Proceed())
	{
		uint const uvOffset = q.CandidateInstanceID();
		uint const primOffset = q.CandidatePrimitiveIndex();
		uint const textureIndex = q.CandidateInstanceContributionToHitGroupIndex();
		float2 const barycentrics = q.CandidateTriangleBarycentrics();
		float const t = q.CandidateTriangleRayT();

		if (CheckAlphaMask(uvOffset + primOffset, textureIndex, barycentrics, t))
		{
			q.CommitNonOpaqueTriangleHit();
		}
	}

	return q.CommittedStatus() != COMMITTED_NOTHING;
}

bool TraceShadows(
	uint2 localID,
	Tile const currentTile,
	bool const bTraceOpaqueTlas,
	bool const bTraceNonOpaqueTlas,
	bool const bTlasIsMixed)
{
	uint2 const pixelCoord = currentTile.location * k_tileSize + localID;

	// use tile mask to decide what pixels will fire a ray
	bool const bActiveLane = WaveMaskToBool(currentTile.mask, localID);
	bool bRayHitSomething = true;
	if (bActiveLane)
	{
		float const depth = t2d_depth[pixelCoord];
		float3 const normal = normalize(t2d_normals[pixelCoord].xyz * 2 - 1.f);

		float2 const uv = pixelCoord * textureSize.zw;
		float4 const homogeneous = mul(viewToWorld, float4(2.0f * float2(uv.x, 1.0f - uv.y) - 1.0f, (depth), 1));
		float3 const worldPos = homogeneous.xyz / homogeneous.w;

		RayDesc ray;
		ray.Origin = worldPos + normal * pixelThickness;
		ray.Direction = -lightDir;
		ray.TMin = currentTile.minT;
		ray.TMax = currentTile.maxT;

		{
			float2 const noise = t2d_blueNoise[pixelCoord % 128].rg + noisePhase;

			ray.Direction = normalize(MapToCone(fmod(noise, 1), ray.Direction, sunSize));
		}

		// reverse ray direction for better traversal 
		if (bUseCascadesForRayT)
		{
			ray.Origin = ray.Origin + ray.Direction * (currentTile.maxT);
			ray.Direction = -ray.Direction;
			ray.TMin = 0;
			ray.TMax = currentTile.maxT - currentTile.minT;
		}

		if (bTraceOpaqueTlas)
		{
			bRayHitSomething = TraceOpaque(ras_opaque, ray);
		}

		if (bTraceNonOpaqueTlas && !bRayHitSomething)
		{
			bRayHitSomething = TraceNonOpaque(ras_nonOpaque, ray);
		}

		if (bTlasIsMixed)
		{
			bRayHitSomething = TraceMixed(ras_opaque, ray);
		}
	}

	return bRayHitSomething;
}

[numthreads(TILE_SIZE_X * TILE_SIZE_Y, 1, 1)]
void TraceOpaqueOnly(uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint2 const localID = FXX_Rmp8x8(localIndex);

	Tile const currentTile = Tile::FromUint(sb_tiles[groupID.x]);

	bool const bRayHitSomething = TraceShadows(
		localID,
		currentTile,
		true,
		false,
		false);

	uint const waveOutput = BoolToWaveMask(bRayHitSomething, localID);
	if (localIndex == 0)
	{
		uint const oldMask = rwt2d_rayHitResults[currentTile.location].x;
		// add results to mask
		rwt2d_rayHitResults[currentTile.location] = waveOutput & oldMask;
	}
}

[numthreads(TILE_SIZE_X * TILE_SIZE_Y, 1, 1)]
void TraceSplitTlas(uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint2 const localID = FXX_Rmp8x8(localIndex);

	Tile const currentTile = Tile::FromUint(sb_tiles[groupID.x]);

	bool const bRayHitSomething = TraceShadows(
		localID,
		currentTile,
		true,
		true,
		false);

	uint const waveOutput = BoolToWaveMask(bRayHitSomething, localID);
	if (localIndex == 0)
	{
		uint const oldMask = rwt2d_rayHitResults[currentTile.location].x;
		// add results to mask
		rwt2d_rayHitResults[currentTile.location] = waveOutput & oldMask;
	}
}

[numthreads(TILE_SIZE_X * TILE_SIZE_Y, 1, 1)]
void TraceMixedTlas(uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint2 const localID = FXX_Rmp8x8(localIndex);

	Tile const currentTile = Tile::FromUint(sb_tiles[groupID.x]);

	bool const bRayHitSomething = TraceShadows(
		localID,
		currentTile,
		false,
		false,
		true);

	uint const waveOutput = BoolToWaveMask(bRayHitSomething, localID);
	if (localIndex == 0)
	{
		uint const oldMask = rwt2d_rayHitResults[currentTile.location].x;
		// add results to mask
		rwt2d_rayHitResults[currentTile.location] = waveOutput & oldMask;
	}
}