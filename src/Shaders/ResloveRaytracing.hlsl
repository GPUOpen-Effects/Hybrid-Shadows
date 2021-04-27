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


//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Texture definitions
//--------------------------------------------------------------------------------------
Texture2D<uint> t2d_hitMaskResults : register(t0);

StructuredBuffer<Tile> sb_tiles : register(t1);

RWTexture2D<float4> rwt2d_output : register(u0);

//--------------------------------------------------------------------------------------
// Main function
//--------------------------------------------------------------------------------------

[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
void main(uint3 globalID : SV_DispatchThreadID, uint3 localID : SV_GroupThreadID, uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	uint const mask = t2d_hitMaskResults[groupID.xy];
	bool const threadHit = WaveMaskToBool(mask, localID.xy);

	float4 const old = float4(0, 0, 0, 0);

	float4 const output = (threadHit == true) ? old : float4(1, 0, 0, 0);

	rwt2d_output[globalID.xy] = output;
}

[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
void blend(uint3 globalID : SV_DispatchThreadID, uint3 localID : SV_GroupThreadID, uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	Tile const currentTile = sb_tiles[groupID.x];

	uint const mask = t2d_hitMaskResults[currentTile.location];
	bool const threadHit = WaveMaskToBool(mask, localID.xy);

	float4 const old = float4(0, 0, 0, 0);

	float4 const output = (threadHit == true) ? old : float4(1, 0, 0, 0);

	uint2 const pixelCoord = currentTile.location * k_tileSize + localID.xy;
	if(WaveMaskToBool(currentTile.mask, localID.xy))
		rwt2d_output[pixelCoord] = output;
}
