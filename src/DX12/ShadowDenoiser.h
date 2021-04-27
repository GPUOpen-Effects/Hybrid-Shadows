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
#pragma once


namespace Raytracing
{

	struct DenoiserControl
	{
		math::Vector3 camPos;
		math::Matrix4 inverseProj;
		math::Matrix4 reprojection;
		math::Matrix4 inverseViewProj;
	};

	class ShadowDenoiser
	{
	public:
		ShadowDenoiser(void);
		~ShadowDenoiser(void);

		void OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps);
		void OnDestroy(void);

		void OnCreateWindowSizeDependentResources(Device* pDevice, uint32_t Width, uint32_t Height);
		void OnDestroyWindowSizeDependentResources(void);

		void BindNormalTexture(Texture& normal);
		void BindDepthTexture(Texture& depth);
		void BindMotionVectorTexture(Texture& motionVector);

		void Denoise(ID3D12GraphicsCommandList* pCommandList, DynamicBufferRing& pDynamicBufferRing, DenoiserControl const& dc, CBV_SRV_UAV& input, CBV_SRV_UAV& output, GPUTimestamps* pGpuTimer);

	private:
		uint32_t m_width;
		uint32_t m_height;

		uint32_t m_momentIndex;
		bool     m_bIsFristFrame;

		Texture* m_pDepth;

		Texture m_depthHistory;
		Texture m_moments[2];
		Texture m_scratch[2];

		Texture m_tileMetaBuffer;
		Texture m_tileBuffer;

		CBV_SRV_UAV m_momentsTable;
		CBV_SRV_UAV m_scratchTable;

		ID3D12RootSignature* m_pPrepareRootSig;
		ID3D12PipelineState* m_pPreparePso;
		CBV_SRV_UAV m_prepareTable;

		ID3D12RootSignature* m_pTileClassificationRootSig;
		ID3D12PipelineState* m_pTileClassificationPso;
		CBV_SRV_UAV m_tileClassificationTable;

		ID3D12RootSignature* m_pFilterPassRootSig;
		ID3D12PipelineState* m_pFilterPassPso[3];
		CBV_SRV_UAV m_filterPassTable;

		StaticResourceViewHeap m_cpuHeap;
		CBV_SRV_UAV m_cpuTable;
	};
}