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
cbuffer cb_debug : register(b1)
{
	uint debugMode;
}

StructuredBuffer<Tile> sb_tiles : register(t0);

RWTexture2D<float4> rwt2d_output : register(u0);

//--------------------------------------------------------------------------------------
// Main function
//--------------------------------------------------------------------------------------

[numthreads(TILE_SIZE_X, TILE_SIZE_Y, 1)]
void main(uint3 globalID : SV_DispatchThreadID, uint3 localID : SV_GroupThreadID, uint localIndex : SV_GroupIndex, uint3 groupID : SV_GroupID)
{
	Tile const currentTile = sb_tiles[groupID.x];

	uint2 const pixelCoord = currentTile.location * k_tileSize + localID.xy;

	float const zScale = 0.01;
	float4 output = float4(0, 0, 0, 0);
	if (debugMode == 0)
	{
		bool const tracePixel = WaveMaskToBool(currentTile.mask, localID.xy);
		output = (tracePixel == false) ? float4(1, 0, 0, 1) : float4(0, 1, 0, 1);
	}
	else if (debugMode == 1)
	{
		float const t = saturate(currentTile.minT * zScale);
		output = float4(t.xxx, 1);
	}
	else if (debugMode == 2)
	{
		float const t = saturate(currentTile.maxT * zScale);
		output = float4(t.xxx, 1);
	}
	else if (debugMode == 3)
	{	
		float const diff = (currentTile.maxT - currentTile.minT);
		float const t = saturate(diff * zScale * 10);
		output = float4(t.xxx, 1);
	}

	rwt2d_output[pixelCoord] = output;
}
