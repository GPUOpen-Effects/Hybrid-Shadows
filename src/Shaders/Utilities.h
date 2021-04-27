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

static const float k_pi = 3.1415926535897932384f;
static const float k_2pi = 2.0f * k_pi;
static const float k_pi_over_2 = 0.5f * k_pi;

// made using a modified version of https://www.asawicki.info/news_952_poisson_disc_generator
static const uint k_poissonDiscSampleCountLow = 8;
static const uint k_poissonDiscSampleCountMid = 16;
static const uint k_poissonDiscSampleCountHigh = 24;
static const uint k_poissonDiscSampleCountUltra = 32;
static const float2 k_poissonDisc[] =
{
	float2(0.640736f, -0.355205f),
	float2(-0.725411f, -0.688316f),
	float2(-0.185095f, 0.722648f),
	float2(0.770596f, 0.637324f),
	float2(-0.921445f, 0.196997f),
	float2(0.076571f, -0.98822f),
	float2(-0.1348f, -0.0908536f),
	float2(0.320109f, 0.257241f),
	float2(0.994021f, 0.109193f),
	float2(0.304934f, 0.952374f),
	float2(-0.698577f, 0.715535f),
	float2(0.548701f, -0.836019f),
	float2(-0.443159f, 0.296121f),
	float2(0.15067f, -0.489731f),
	float2(-0.623829f, -0.208167f),
	float2(-0.294778f, -0.596545f),
	float2(0.334086f, -0.128208f),
	float2(-0.0619831f, 0.311747f),
	float2(0.166112f, 0.61626f),
	float2(-0.289127f, -0.957291f),
	float2(-0.98748f, -0.157745f),
	float2(0.637501f, 0.0651571f),
	float2(0.971376f, -0.237545f),
	float2(-0.0170599f, 0.98059f),
	float2(-0.442564f, 0.896737f),
	float2(0.48619f, 0.518723f),
	float2(-0.725272f, 0.419965f),
	float2(0.781417f, -0.624009f),
	float2(-0.899227f, -0.437482f),
	float2(0.769219f, 0.33372f),
	float2(-0.414411f, 0.00375378f),
	float2(0.262856f, -0.759514f),
};

//--------------------------------------------------------------------------------------
// helper functions
//--------------------------------------------------------------------------------------


float2x3 CreateTangentVectors(float3 normal)
{
	float3 up = abs(normal.z) < 0.99999f ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);

	float2x3 tangents;

	tangents[0] = normalize(cross(up, normal));
	tangents[1] = cross(normal, tangents[0]);

	return tangents;
}

float3 MapToCone(float2 s, float3 n, float radius)
{

	const float2 offset = 2.0f * s - float2(1.0f, 1.0f);

	if (offset.x == 0.0f && offset.y == 0.0f)
	{
		return n;
	}

	float theta, r;

	if (abs(offset.x) > abs(offset.y))
	{
		r = offset.x;
		theta = k_pi / 4.0f * (offset.y / offset.x);
	}
	else
	{
		r = offset.y;
		theta = k_pi_over_2 * (1.0f - 0.5f * (offset.x / offset.y));
	}

	const float2 uv = float2(radius * r * cos(theta), radius * r * sin(theta));

	const float2x3 tangents = CreateTangentVectors(n);

	return n + uv.x * tangents[0] + uv.y * tangents[1];
}
