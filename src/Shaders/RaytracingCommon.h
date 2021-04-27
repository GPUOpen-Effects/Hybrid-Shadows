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

#define TILE_SIZE_X 8
#define TILE_SIZE_Y 4
static const float k_pushOff = 4e-2f;

static const uint2 k_tileSize = uint2(TILE_SIZE_X, TILE_SIZE_Y);

//--------------------------------------------------------------------------------------
// Constant Buffer
//--------------------------------------------------------------------------------------
cbuffer cb_controls : register(b0)
{
	float4 textureSize;
	float3 lightDir;
	float  skyHeight;

	float pixelThickness;
	float sunSize;
	float noisePhase;
	bool  bRejectLitPixels;

	uint   cascadeCount;
	uint   activeCascades;
	uint   tileTolerance;
	float  blockerOffset;

	float  cascadePixelSize;
	float  cascadeSize;
	float  sunSizeLightSpace;
	bool   bUseCascadesForRayT;

	float4 cascadeScale[4];
	float4 cascadeOffset[4];

	float4x4 viewToWorld;
	float4x4 lightView;
	float4x4 inverseLightView;
};

//--------------------------------------------------------------------------------------
// I/O Structures
//--------------------------------------------------------------------------------------

struct UV
{
	float2 uv0;

	float16_t2 uv01;
	float16_t2 uv02;
};

struct Tile
{
	static Tile Create(uint2 const id)
	{
		Tile const t = { id, 0, k_pushOff, skyHeight };
		return t;
	}

	static uint4 ToUint(Tile const t)
	{
		uint4 const ui = uint4(
			(uint(t.location.y) << 16) | uint(t.location.x), 
			t.mask,
			asuint(t.minT),
			asuint(t.maxT));
		return ui;
	}

	static Tile FromUint(uint4 const ui)
	{
		uint16_t2 const id = uint16_t2(ui.x & 0xffff, (ui.x >> 16) & 0xffff);
		Tile const t = { id, ui.y, asfloat(ui.z), asfloat(ui.w) };
		return t;
	}

	uint16_t2 location;
	uint mask;

	float minT;
	float maxT;
};

//--------------------------------------------------------------------------------------
// helper functions
//--------------------------------------------------------------------------------------

uint LaneIdToBitShift(uint2 localID)
{
	return localID.y * k_tileSize.x + localID.x;
}

uint BoolToWaveMask(bool b, uint2 localID)
{
	uint const value = b << LaneIdToBitShift(localID);
	return WaveActiveBitOr(value);
}

bool WaveMaskToBool(uint mask, uint2 localID)
{
	return (1 << LaneIdToBitShift(localID)) & mask;
}


float NextUlp(float f)
{
	uint const fInt = asuint(f);
	// keep the exponent and set the mantissa to one
	// uint const fError = (fInt & 0xFF800000) | (((fInt & 0x7FFFFF) + 1) & 0x7FFFFF);
	uint const fError = fInt + 1;

	return asfloat(fError);
}

// From ffx_a.h

uint FFX_BitfieldExtract(uint src, uint off, uint bits) { uint mask = (1 << bits) - 1; return (src >> off) & mask; } // ABfe
uint FFX_BitfieldInsert(uint src, uint ins, uint bits) { uint mask = (1 << bits) - 1; return (ins & mask) | (src & (~mask)); } // ABfiM

 //  543210
 //  ======
 //  ..xxx.
 //  yy...y
//uint2 FXX_Rmp8x8(uint a) 
//{ 
//	return uint2(
//		FFX_BitfieldExtract(a, 1u, 3u),
//		FFX_BitfieldInsert(FFX_BitfieldExtract(a, 3u, 3u), a, 1u)
//		); 
//}

 // More complex remap 64x1 to 8x8 which is necessary for 2D wave reductions.
 //  543210
 //  ======
 //  .xx..x
 //  y..yy.
 // Details,
 //  LANE TO 8x8 MAPPING
 //  ===================
 //  00 01 08 09 10 11 18 19 
 //  02 03 0a 0b 12 13 1a 1b
 //  04 05 0c 0d 14 15 1c 1d
 //  06 07 0e 0f 16 17 1e 1f 
 //  20 21 28 29 30 31 38 39 
 //  22 23 2a 2b 32 33 3a 3b
 //  24 25 2c 2d 34 35 3c 3d
 //  26 27 2e 2f 36 37 3e 3f 
uint2 FXX_Rmp8x8(uint a) 
{ 
	return uint2(
		FFX_BitfieldInsert(FFX_BitfieldExtract(a, 2u, 3u), a, 1u),
		FFX_BitfieldInsert(FFX_BitfieldExtract(a, 3u, 3u), FFX_BitfieldExtract(a, 1u, 2u), 2u));
}