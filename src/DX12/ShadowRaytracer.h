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

#include "ShadowDenoiser.h"

namespace Raytracing
{
	class TLAS;

	enum class ClassifyMethod
	{
		ByNormals,
		ByCascadeRange,
		ByCascades,
	};

	enum class TraceMethod
	{
		ForceOpaque,
		SplitTlas,
		MixedTlas
	};

	struct TraceControls
	{
		float textureWidth;
		float textureHeight;
		float textureInvWidth;
		float textureInvHeight;
		float lightDir[3];
		float  skyHeight;

		float pixelThickness;
		float sunSize;
		float noisePhase;
		bool  bRejectLitPixels;

		uint32_t cascadeCount;
		uint32_t activeCascades;
		uint32_t tileTolerance;
		float  blockerOffset;

		float  cascadePixelSize;
		float  cascadeSize;
		float  sunSizeLightSpace;
		bool   bUseCascadesForRayT;

		math::Vector4 cascadeScale[4];
		math::Vector4 cascadeOffset[4];

		math::Matrix4 viewToWorld;
		math::Matrix4 lightView;
		math::Matrix4 inverseLightView;
	};

	class ShadowTrace
	{
	public:
		ShadowTrace(void);
		~ShadowTrace(void);

		void OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps);
		void OnDestroy(void);

		void OnCreateWindowSizeDependentResources(Device* pDevice, uint32_t Width, uint32_t Height);
		void OnDestroyWindowSizeDependentResources(void);

		void BindNormalTexture(Texture& normal);
		void BindDepthTexture(Texture& depth);
		void BindShadowTexture(Texture& shadow);
		void BindMotionVectorTexture(Texture& motionVector);

		void SetBlueNoise(Texture& noise);
		void SetUVBuffer(Texture& buffer);


		D3D12_GPU_VIRTUAL_ADDRESS BuildTraceControls(DynamicBufferRing& pDynamicBufferRing, Light const& light, math::Matrix4 const& viewToWorld, TraceControls& tc);

		void Classify(ID3D12GraphicsCommandList* pCommandList, ClassifyMethod method, D3D12_GPU_VIRTUAL_ADDRESS traceControls);
		void Trace(ID3D12GraphicsCommandList* pCommandList, TLAS const& tlas0, TLAS const& tlas1, CBV_SRV_UAV& maskTextures, TraceMethod method, D3D12_GPU_VIRTUAL_ADDRESS traceControls);

		void ResolveHitsToShadowMask(ID3D12GraphicsCommandList* pCommandList, CBV_SRV_UAV& target);
		void BlendHitsToShadowMask(ID3D12GraphicsCommandList* pCommandList, CBV_SRV_UAV& target);
		void DenoiseHitsToShadowMask(ID3D12GraphicsCommandList *pCommandList, DynamicBufferRing& pDynamicBufferRing, Camera const& cam, CBV_SRV_UAV& target, GPUTimestamps* pGpuTimer = nullptr);

		void DebugTileClassification(ID3D12GraphicsCommandList* pCommandList, uint32_t debugMode, CBV_SRV_UAV& target);

	private:
		uint32_t m_width;
		uint32_t m_height;

		Texture m_rayHitTexture;
		Texture m_blueNoise;
		Texture m_workQueue;
		Texture m_workQueueCount;

		bool m_bIsRayHitShaderRead;

		ShadowDenoiser m_denoiser;

		ID3D12RootSignature* m_pRaytracerRootSig;
		ID3D12PipelineState* m_pRaytracerPso[3];
		CBV_SRV_UAV m_raytracerTable;

		ID3D12RootSignature* m_pClassifyRootSig;
		ID3D12PipelineState* m_pClassifyPso[3];
		CBV_SRV_UAV m_classifyTable;

		ID3D12RootSignature* m_pResolveRootSig;
		ID3D12PipelineState* m_pResolvePso[2];
		CBV_SRV_UAV m_resolveTable;

		ID3D12RootSignature* m_pDebugRootSig;
		ID3D12PipelineState* m_pDebugPso;
		CBV_SRV_UAV m_debugTable;

		ID3D12CommandSignature* m_pDispatchIndirect;

		StaticResourceViewHeap m_cpuHeap;
		CBV_SRV_UAV m_cpuTable;
	};
}