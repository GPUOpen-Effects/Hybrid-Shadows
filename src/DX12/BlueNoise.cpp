// AMD SampleDX12 sample code
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

#include "stdafx.h"

#include "BlueNoise.h"

namespace _1spp
{
#include "../../../samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
}


Texture CreateBlueNoiseTexture(Device* device, UploadHeap& heap)
{
	byte blueNoise[128][128][4] = {};

	for (int x = 0; x < 128; ++x)
	{
		for (int y = 0; y < 128; ++y)
		{
			float const f0 = _1spp::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 0);
			float const f1 = _1spp::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 1);
			float const f2 = _1spp::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 2);
			float const f3 = _1spp::samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp(x, y, 0, 3);

			blueNoise[x][y][0] = static_cast<byte>(f0 * UCHAR_MAX);
			blueNoise[x][y][1] = static_cast<byte>(f1 * UCHAR_MAX);
			blueNoise[x][y][2] = static_cast<byte>(f2 * UCHAR_MAX);
			blueNoise[x][y][3] = static_cast<byte>(f3 * UCHAR_MAX);
		}
	}

	IMG_INFO info = { };
	info.width = 128;
	info.height = 128;
	info.depth = 1;
	info.arraySize = 1;
	info.mipMapCount = 1;
	info.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.bitCount = 32;

	Texture tex;
	tex.InitFromData(device, "Blue Noise", heap, info, blueNoise);

	return std::move(tex);
}
