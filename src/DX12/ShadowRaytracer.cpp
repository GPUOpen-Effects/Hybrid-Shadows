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

#include "ShadowRaytracer.h"
#include "Raytracer.h"

namespace
{
	constexpr uint32_t DivRoundUp(uint32_t a, uint32_t b)
	{
		return (a + b - 1) / b;
	}

	math::Vector3 CreateTangentVector(math::Vector3 normal)
	{
		math::Vector3 up = abs(normal.getZ()) < 0.99999f ? math::Vector3(0.0, 0.0, 1.0) : math::Vector3(1.0, 0.0, 0.0);

		return math::SSE::normalize(math::SSE::cross(up, normal));
	}
}

namespace Raytracing
{
	constexpr uint32_t k_tileSizeX = 8;
	constexpr uint32_t k_tileSizeY = 4;

	ShadowTrace::ShadowTrace(void)
		: m_width(0)
		, m_height(0)
		, m_rayHitTexture()
		, m_blueNoise()
		, m_workQueue()
		, m_workQueueCount()
		, m_bIsRayHitShaderRead(true)
		, m_pRaytracerRootSig(nullptr)
		, m_pRaytracerPso{ nullptr }
		, m_raytracerTable()
		, m_pClassifyRootSig(nullptr)
		, m_pClassifyPso{ nullptr }
		, m_classifyTable()
		, m_pResolveRootSig(nullptr)
		, m_pResolvePso{ nullptr }
		, m_resolveTable()
		, m_pDebugRootSig(nullptr)
		, m_pDebugPso(nullptr)
		, m_debugTable()
		, m_pDispatchIndirect(nullptr)
		, m_cpuHeap()
		, m_cpuTable()
	{
	}

	ShadowTrace::~ShadowTrace(void)
	{
	}

	void ShadowTrace::OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps)
	{
		m_cpuHeap.OnCreate(pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, true);
		m_cpuHeap.AllocDescriptor(16, &m_cpuTable);

		{
			D3D12_INDIRECT_ARGUMENT_DESC args[] =
			{
				{D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH}
			};
			D3D12_COMMAND_SIGNATURE_DESC desc = {};
			desc.ByteStride = 3 * sizeof(uint32_t);
			desc.NumArgumentDescs = 1;
			desc.pArgumentDescs = args;

			pDevice->GetDevice()->CreateCommandSignature(
				&desc,
				nullptr,
				IID_PPV_ARGS(&m_pDispatchIndirect));
		}

		// classfiy
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(6, &m_classifyTable);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3u, 0u);

			CD3DX12_ROOT_PARAMETER rootParameters[2] = {};
			rootParameters[0].InitAsConstantBufferView(0);
			rootParameters[1].InitAsDescriptorTable(2, descriptorRanges);

			CD3DX12_STATIC_SAMPLER_DESC staticSamplerDescs[2] = {};
			staticSamplerDescs[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
			//staticSamplerDescs[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
			staticSamplerDescs[1].Init(1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
			staticSamplerDescs[1].ComparisonFunc = 	D3D12_COMPARISON_FUNC_LESS;

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(2, rootParameters, 2, staticSamplerDescs);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pClassifyRootSig))
			);
			SetName(m_pClassifyRootSig, "m_pClassifyRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pClassifyRootSig;

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("Classify.hlsl", NULL, "ClassifyByNormal", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pClassifyPso[0]));
			SetName(m_pClassifyPso[0], "m_pClassifyPso Normals");

			CompileShaderFromFile("Classify.hlsl", NULL, "ClassifyByCascadeRange", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pClassifyPso[1]));
			SetName(m_pClassifyPso[1], "m_pClassifyPso Cascade Range");

			CompileShaderFromFile("Classify.hlsl", NULL, "ClassifyByCascades", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pClassifyPso[2]));
			SetName(m_pClassifyPso[2], "m_pClassifyPso Cascades");
		}

		// classfiy debug
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_debugTable);

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("ClassifyDebug.hlsl", NULL, "main", "-enable-16bit-types -T cs_6_5", &shaderByteCode);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

			CD3DX12_ROOT_PARAMETER rootParameters[3] = {};
			rootParameters[0].InitAsConstants(1, 1);
			rootParameters[1].InitAsDescriptorTable(1, descriptorRanges);
			rootParameters[2].InitAsDescriptorTable(1, descriptorRanges + 1);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(3, rootParameters, 0, nullptr);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pDebugRootSig))
			);
			SetName(m_pDebugRootSig, "m_pDebugRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pDebugRootSig;
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pDebugPso));
			SetName(m_pDebugPso, "m_pDebugPso");
		}

		// raytracer
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(6, &m_raytracerTable);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[3] = {};
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);
			descriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0u, 2u);

			CD3DX12_ROOT_PARAMETER rootParameters[5] = {};
			rootParameters[0].InitAsConstantBufferView(0);
			rootParameters[1].InitAsShaderResourceView(0, 1);
			rootParameters[2].InitAsShaderResourceView(1, 1);
			rootParameters[3].InitAsDescriptorTable(2, descriptorRanges);
			rootParameters[4].InitAsDescriptorTable(1, descriptorRanges + 2);

			CD3DX12_STATIC_SAMPLER_DESC staticSamplerDescs[1] = {};
			staticSamplerDescs[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(5, rootParameters, 1, staticSamplerDescs);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRaytracerRootSig))
			);
			SetName(m_pRaytracerRootSig, "m_pRaytracerRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pRaytracerRootSig;

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("ShadowRaytrace.hlsl", nullptr, "TraceOpaqueOnly", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pRaytracerPso[0]));
			SetName(m_pRaytracerPso[0], "m_pRaytracerPso Opaque");

			CompileShaderFromFile("ShadowRaytrace.hlsl", nullptr, "TraceSplitTlas", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pRaytracerPso[1]));
			SetName(m_pRaytracerPso[1], "m_pRaytracerPso Opaque + Non");

			CompileShaderFromFile("ShadowRaytrace.hlsl", nullptr, "TraceMixedTlas", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pRaytracerPso[2]));
			SetName(m_pRaytracerPso[2], "m_pRaytracerPso Mixed");
		}

		// resolve
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(2, &m_resolveTable);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

			CD3DX12_ROOT_PARAMETER rootParameters[3] = {};
			rootParameters[0].InitAsConstantBufferView(0);
			rootParameters[1].InitAsDescriptorTable(1, descriptorRanges);
			rootParameters[2].InitAsDescriptorTable(1, descriptorRanges + 1);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(3, rootParameters, 0, nullptr);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pResolveRootSig))
			);
			SetName(m_pResolveRootSig, "m_pResolveRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pResolveRootSig;

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("ResloveRaytracing.hlsl", NULL, "main", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pResolvePso[0]));
			SetName(m_pResolvePso[0], "m_pResolvePso");

			CompileShaderFromFile("ResloveRaytracing.hlsl", NULL, "blend", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pResolvePso[1]));
			SetName(m_pResolvePso[1], "m_pResolvePso blend");
		}

		m_workQueueCount.InitBuffer(pDevice, "Work Queue Counter", &CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * 3, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), sizeof(uint32_t), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
		m_workQueueCount.CreateBufferUAV(4, nullptr, &m_classifyTable);

		m_denoiser.OnCreate(pDevice, pResourceViewHeaps);
	}

	void ShadowTrace::OnDestroy(void)
	{
		if (m_pRaytracerRootSig)
		{
			m_pRaytracerRootSig->Release();
			m_pRaytracerRootSig = nullptr;
		}

		if (m_pRaytracerPso[0])
		{
			m_pRaytracerPso[0]->Release();
			m_pRaytracerPso[0] = nullptr;
		}

		if (m_pRaytracerPso[1])
		{
			m_pRaytracerPso[1]->Release();
			m_pRaytracerPso[1] = nullptr;
		}

		if (m_pRaytracerPso[2])
		{
			m_pRaytracerPso[2]->Release();
			m_pRaytracerPso[2] = nullptr;
		}

		if (m_pClassifyRootSig)
		{
			m_pClassifyRootSig->Release();
			m_pClassifyRootSig = nullptr;
		}

		if (m_pClassifyPso[0])
		{
			m_pClassifyPso[0]->Release();
			m_pClassifyPso[0] = nullptr;
		}

		if (m_pClassifyPso[1])
		{
			m_pClassifyPso[1]->Release();
			m_pClassifyPso[1] = nullptr;
		}

		if (m_pClassifyPso[2])
		{
			m_pClassifyPso[2]->Release();
			m_pClassifyPso[2] = nullptr;
		}

		if (m_pResolveRootSig)
		{
			m_pResolveRootSig->Release();
			m_pResolveRootSig = nullptr;
		}

		if (m_pResolvePso[0])
		{
			m_pResolvePso[0]->Release();
			m_pResolvePso[0] = nullptr;
		}

		if (m_pResolvePso[1])
		{
			m_pResolvePso[1]->Release();
			m_pResolvePso[1] = nullptr;
		}

		if (m_pDebugRootSig)
		{
			m_pDebugRootSig->Release();
			m_pDebugRootSig = nullptr;
		}

		if (m_pDebugPso)
		{
			m_pDebugPso->Release();
			m_pDebugPso = nullptr;
		}

		if (m_pDispatchIndirect)
		{
			m_pDispatchIndirect->Release();
			m_pDispatchIndirect = nullptr;
		}

		m_cpuHeap.OnDestroy();

		m_workQueueCount.OnDestroy();

		m_denoiser.OnDestroy();
	}

	void ShadowTrace::OnCreateWindowSizeDependentResources(Device* pDevice, uint32_t Width, uint32_t Height)
	{
		uint32_t const xTiles = DivRoundUp(Width, k_tileSizeX);
		uint32_t const yTiles = DivRoundUp(Height, k_tileSizeY);
		CD3DX12_RESOURCE_DESC const desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_UINT,
			xTiles,
			yTiles,
			1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		m_rayHitTexture.Init(pDevice, "Ray hit texture", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

		m_rayHitTexture.CreateUAV(5, &m_classifyTable);
		m_rayHitTexture.CreateUAV(5, &m_raytracerTable);
		m_rayHitTexture.CreateSRV(0, &m_resolveTable);
		m_rayHitTexture.CreateUAV(0, &m_cpuTable);


		uint32_t const tileCount = xTiles * yTiles;
		size_t const tileSize = sizeof(uint32_t) * 4;
		m_workQueue.InitBuffer(
			pDevice, 
			"Work Queue", 
			&CD3DX12_RESOURCE_DESC::Buffer(tileSize * tileCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
			tileSize,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		m_width = Width;
		m_height = Height;

		m_workQueue.CreateBufferUAV(3, nullptr, &m_classifyTable);
		m_workQueue.CreateSRV(0, &m_debugTable);
		m_workQueue.CreateSRV(1, &m_resolveTable);
		m_workQueue.CreateSRV(3, &m_raytracerTable);

		m_denoiser.OnCreateWindowSizeDependentResources(pDevice, Width, Height);
	}

	void ShadowTrace::OnDestroyWindowSizeDependentResources(void)
	{
		m_rayHitTexture.OnDestroy();
		m_workQueue.OnDestroy();

		m_denoiser.OnDestroyWindowSizeDependentResources();
	}

	void ShadowTrace::BindNormalTexture(Texture& normal)
	{
		normal.CreateSRV(1, &m_classifyTable);
		normal.CreateSRV(1, &m_raytracerTable);

		m_denoiser.BindNormalTexture(normal);
	}

	void ShadowTrace::BindDepthTexture(Texture& depth)
	{
		depth.CreateSRV(0, &m_classifyTable);
		depth.CreateSRV(0, &m_raytracerTable);

		m_denoiser.BindDepthTexture(depth);
	}

	void ShadowTrace::BindShadowTexture(Texture& shadow)
	{
		shadow.CreateSRV(2, &m_classifyTable);
	}

	void ShadowTrace::BindMotionVectorTexture(Texture& motionVector)
	{
		m_denoiser.BindMotionVectorTexture(motionVector);
	}

	void ShadowTrace::SetBlueNoise(Texture& noise)
	{
		noise.CreateSRV(2, &m_raytracerTable);
	}

	void ShadowTrace::SetUVBuffer(Texture& buffer)
	{
		if (buffer.GetResource())
		{
			buffer.CreateSRV(4, &m_raytracerTable);
		}
	}

	D3D12_GPU_VIRTUAL_ADDRESS ShadowTrace::BuildTraceControls(DynamicBufferRing& pDynamicBufferRing, Light const& light, math::Matrix4 const& viewToWorld, TraceControls& tc)
	{
		math::Vector3 const lightDir = math::Vector3(light.direction[0], light.direction[1], light.direction[2]);
		math::Vector3 const coneVec = math::SSE::normalize(lightDir) + CreateTangentVector(lightDir) * tc.sunSize;
		math::Vector3 const lightSpaceConeVec = (tc.lightView * math::Vector4(coneVec, 0)).getXYZ();

		tc.sunSizeLightSpace = math::length(math::Vector2(lightSpaceConeVec.getX(), lightSpaceConeVec.getY())) / lightSpaceConeVec.getZ();
		tc.textureWidth = static_cast<float>(m_width);
		tc.textureHeight = static_cast<float>(m_height);
		tc.textureInvWidth = 1.f / m_width;
		tc.textureInvHeight = 1.f / m_height;

		tc.lightDir[0] = -light.direction[0];
		tc.lightDir[1] = -light.direction[1];
		tc.lightDir[2] = -light.direction[2];
		tc.skyHeight = FLT_MAX;
		tc.pixelThickness = 1e-4f;

		tc.viewToWorld = viewToWorld;
		tc.inverseLightView = math::affineInverse(tc.lightView);

		return pDynamicBufferRing.AllocConstantBuffer(sizeof(tc), &tc);
	}

	void ShadowTrace::Classify(ID3D12GraphicsCommandList* pCommandList, ClassifyMethod method, D3D12_GPU_VIRTUAL_ADDRESS traceControls)
	{
		UserMarker marker(pCommandList, "Classify tiles");

		// clear work counter by using WriteBufferImmediate
		D3D12_RESOURCE_BARRIER preClear[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_workQueueCount.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST),
		};
		pCommandList->ResourceBarrier(ARRAYSIZE(preClear), preClear);

		ID3D12GraphicsCommandList4* pCmdList4 = nullptr;
		pCommandList->QueryInterface(&pCmdList4);

		D3D12_GPU_VIRTUAL_ADDRESS address = m_workQueueCount.GetResource()->GetGPUVirtualAddress();
		D3D12_WRITEBUFFERIMMEDIATE_PARAMETER const params[3] =
		{
			{address + sizeof(uint32_t) * 0, 0},
			{address + sizeof(uint32_t) * 1, 1},
			{address + sizeof(uint32_t) * 2, 1},
		};
		pCmdList4->WriteBufferImmediate(3, params, nullptr);
		pCmdList4->Release();

		D3D12_RESOURCE_BARRIER postClear[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_workQueueCount.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_workQueue.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pCommandList->ResourceBarrier(ARRAYSIZE(postClear), postClear);

		// Bind the descriptor heaps and root signature
		pCommandList->SetComputeRootSignature(m_pClassifyRootSig);

		// Bind the pipeline state
		//
		pCommandList->SetPipelineState(m_pClassifyPso[(int)method]);

		// Bind the descriptor set
		//
		pCommandList->SetComputeRootConstantBufferView(0, traceControls);
		pCommandList->SetComputeRootDescriptorTable(1, m_classifyTable.GetGPU());


		// Dispatch
		//
		uint32_t const ThreadGroupCountX = DivRoundUp(m_width, k_tileSizeX);
		uint32_t const ThreadGroupCountY = DivRoundUp(m_height, k_tileSizeY);
		pCommandList->Dispatch(ThreadGroupCountX, ThreadGroupCountY, 1);

	}

	void ShadowTrace::Trace(ID3D12GraphicsCommandList* pCommandList, TLAS const& tlas0, TLAS const& tlas1, CBV_SRV_UAV& maskTextures, TraceMethod method, D3D12_GPU_VIRTUAL_ADDRESS traceControls)
	{
		UserMarker marker(pCommandList, "Trace shadows");

		D3D12_RESOURCE_BARRIER preTrace[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_rayHitTexture.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_workQueue.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_workQueueCount.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
		};
		pCommandList->ResourceBarrier(ARRAYSIZE(preTrace), preTrace);

		// Bind the descriptor heaps and root signature
		pCommandList->SetComputeRootSignature(m_pRaytracerRootSig);

		// Bind the pipeline state
		//
		pCommandList->SetPipelineState(m_pRaytracerPso[(int)method]);

		// Bind the descriptor set
		//
		pCommandList->SetComputeRootConstantBufferView(0, traceControls);
		pCommandList->SetComputeRootShaderResourceView(1, tlas0.GetGpuAddress());
		pCommandList->SetComputeRootShaderResourceView(2, tlas1.GetGpuAddress());
		pCommandList->SetComputeRootDescriptorTable(3, m_raytracerTable.GetGPU());
		pCommandList->SetComputeRootDescriptorTable(4, maskTextures.GetGPU());

		assert(tlas0.GetGpuAddress() != 0);
		assert(tlas1.GetGpuAddress() != 0);
		// Dispatch
		//
		pCommandList->ExecuteIndirect(
			m_pDispatchIndirect,
			1,
			m_workQueueCount.GetResource(), 0,
			nullptr, 0);

		m_bIsRayHitShaderRead = false;
	}

	void ShadowTrace::ResolveHitsToShadowMask(ID3D12GraphicsCommandList* pCommandList, CBV_SRV_UAV& target)
	{
		UserMarker marker(pCommandList, "Resolve shadow hits");

		if (m_bIsRayHitShaderRead == false)
		{
			D3D12_RESOURCE_BARRIER preResolve[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_rayHitTexture.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCommandList->ResourceBarrier(ARRAYSIZE(preResolve), preResolve);
			m_bIsRayHitShaderRead = true;
		}

		// Bind the descriptor heaps and root signature
		pCommandList->SetComputeRootSignature(m_pResolveRootSig);

		// Bind the pipeline state
		//
		pCommandList->SetPipelineState(m_pResolvePso[0]);

		// Bind the descriptor set
		//
		//pCommandList->SetComputeRootConstantBufferView(0, traceControls);
		pCommandList->SetComputeRootDescriptorTable(1, m_resolveTable.GetGPU());
		pCommandList->SetComputeRootDescriptorTable(2, target.GetGPU());


		// Dispatch
		//
		uint32_t const ThreadGroupCountX = DivRoundUp(m_width, k_tileSizeX);
		uint32_t const ThreadGroupCountY = DivRoundUp(m_height, k_tileSizeY);
		pCommandList->Dispatch(ThreadGroupCountX, ThreadGroupCountY, 1);
	}

	void ShadowTrace::BlendHitsToShadowMask(ID3D12GraphicsCommandList* pCommandList, CBV_SRV_UAV& target)
	{
		UserMarker marker(pCommandList, "Blend shadow hits");

		if (m_bIsRayHitShaderRead == false)
		{
			D3D12_RESOURCE_BARRIER preResolve[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_rayHitTexture.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCommandList->ResourceBarrier(ARRAYSIZE(preResolve), preResolve);
			m_bIsRayHitShaderRead = true;
		}

		// Bind the descriptor heaps and root signature
		pCommandList->SetComputeRootSignature(m_pResolveRootSig);

		// Bind the pipeline state
		//
		pCommandList->SetPipelineState(m_pResolvePso[1]);

		// Bind the descriptor set
		//
		//pCommandList->SetComputeRootConstantBufferView(0, traceControls);
		pCommandList->SetComputeRootDescriptorTable(1, m_resolveTable.GetGPU());
		pCommandList->SetComputeRootDescriptorTable(2, target.GetGPU());


		// Dispatch
		//
		pCommandList->ExecuteIndirect(
			m_pDispatchIndirect,
			1,
			m_workQueueCount.GetResource(), 0,
			nullptr, 0);
	}

	void ShadowTrace::DenoiseHitsToShadowMask(ID3D12GraphicsCommandList* pCommandList, DynamicBufferRing& pDynamicBufferRing, Camera const& cam, CBV_SRV_UAV& target, GPUTimestamps* pGpuTimer)
	{
		if (m_bIsRayHitShaderRead == false)
		{
			D3D12_RESOURCE_BARRIER preResolve[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(m_rayHitTexture.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			pCommandList->ResourceBarrier(ARRAYSIZE(preResolve), preResolve);
			m_bIsRayHitShaderRead = true;
		}

		DenoiserControl dc = {};
		dc.camPos = cam.GetPosition().getXYZ();
		dc.inverseProj = math::inverse(cam.GetProjection());
		dc.inverseViewProj = math::affineInverse(cam.GetView()) * dc.inverseProj;
		dc.reprojection = cam.GetProjection() * (cam.GetPrevView() * dc.inverseViewProj);

		m_denoiser.Denoise(pCommandList, pDynamicBufferRing, dc, m_resolveTable, target, pGpuTimer);
	}

	void ShadowTrace::DebugTileClassification(ID3D12GraphicsCommandList* pCommandList, uint32_t debugMode, CBV_SRV_UAV& target)
	{
		UserMarker marker(pCommandList, "Debug tiles");

		// Bind the descriptor heaps and root signature
		pCommandList->SetComputeRootSignature(m_pDebugRootSig);

		// Bind the pipeline state
		//
		pCommandList->SetPipelineState(m_pDebugPso);

		// Bind the descriptor set
		//
		pCommandList->SetComputeRoot32BitConstant(0, debugMode, 0);
		pCommandList->SetComputeRootDescriptorTable(1, m_debugTable.GetGPU());
		pCommandList->SetComputeRootDescriptorTable(2, target.GetGPU());

		// Dispatch
		//
		pCommandList->ExecuteIndirect(
			m_pDispatchIndirect,
			1,
			m_workQueueCount.GetResource(), 0,
			nullptr, 0);
	}
}