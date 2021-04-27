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

#include "ShadowDenoiser.h"

namespace
{
	constexpr uint32_t DivRoundUp(uint32_t a, uint32_t b)
	{
		return (a + b - 1) / b;
	}
}

namespace Raytracing
{
	constexpr uint32_t k_tileSizeX = 8;
	constexpr uint32_t k_tileSizeY = 4;

	ShadowDenoiser::ShadowDenoiser(void)
		: m_momentIndex(0)
	{
	}

	ShadowDenoiser::~ShadowDenoiser(void)
	{
	}

	void ShadowDenoiser::OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps)
	{
		pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(4, &m_momentsTable);
		pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(4, &m_scratchTable);

		// prepare
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_prepareTable);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[2];
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

			CD3DX12_ROOT_PARAMETER rootParameters[3];
			rootParameters[0].InitAsConstantBufferView(0);
			rootParameters[1].InitAsDescriptorTable(1, descriptorRanges);
			rootParameters[2].InitAsDescriptorTable(1, descriptorRanges + 1);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(3, rootParameters, 0, nullptr);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pPrepareRootSig))
			);
			SetName(m_pPrepareRootSig, "m_pPrepareRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pPrepareRootSig;

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("prepare_shadow_mask_d3d12.hlsl", NULL, "main", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pPreparePso));
			SetName(m_pPreparePso, "m_pPreparePso");
		}

		// classification
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(8, &m_tileClassificationTable);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[4];
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2u, 0u);
			descriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u, 1u);
			descriptorRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u, 1u);

			CD3DX12_ROOT_PARAMETER rootParameters[4];
			rootParameters[0].InitAsConstantBufferView(0);
			rootParameters[1].InitAsDescriptorTable(2, descriptorRanges);
			rootParameters[2].InitAsDescriptorTable(1, descriptorRanges + 2);
			rootParameters[3].InitAsDescriptorTable(1, descriptorRanges + 3);

			CD3DX12_STATIC_SAMPLER_DESC staticSamplerDescs[1];
			staticSamplerDescs[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(4, rootParameters, 1, staticSamplerDescs);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pTileClassificationRootSig))
			);
			SetName(m_pTileClassificationRootSig, "m_pTileClassificationRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pTileClassificationRootSig;

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("tile_classification_d3d12.hlsl", NULL, "main", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pTileClassificationPso));
			SetName(m_pPreparePso, "m_pTileClassificationPso");
		}

		// filter
		{
			// Alloc descriptors
			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor(3, &m_filterPassTable);

			// Create root signature
			//
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[3];
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3u, 0u);
			descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u, 1u);
			descriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

			CD3DX12_ROOT_PARAMETER rootParameters[4];
			rootParameters[0].InitAsConstantBufferView(0);
			rootParameters[1].InitAsDescriptorTable(1, descriptorRanges);
			rootParameters[2].InitAsDescriptorTable(1, descriptorRanges + 1);
			rootParameters[3].InitAsDescriptorTable(1, descriptorRanges + 2);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
			rootSignatureDesc.Init(4, rootParameters, 0, nullptr);

			ID3DBlob* pOutBlob, * pErrorBlob = NULL;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
			ThrowIfFailed(
				pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_pFilterPassRootSig))
			);
			SetName(m_pFilterPassRootSig, "m_pFilterPassRootSig");

			pOutBlob->Release();
			if (pErrorBlob)
				pErrorBlob->Release();

			D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {};
			pipelineStateDesc.pRootSignature = m_pFilterPassRootSig;

			// Compile shader
			D3D12_SHADER_BYTECODE shaderByteCode = {};
			CompileShaderFromFile("filter_soft_shadows_pass_d3d12.hlsl", NULL, "Pass0", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pFilterPassPso[0]));
			SetName(m_pFilterPassPso[0], "m_pFilterPassPso[0]");

			CompileShaderFromFile("filter_soft_shadows_pass_d3d12.hlsl", NULL, "Pass1", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pFilterPassPso[1]));
			SetName(m_pFilterPassPso[1], "m_pFilterPassPso[1]");

			CompileShaderFromFile("filter_soft_shadows_pass_d3d12.hlsl", NULL, "Pass2", "-enable-16bit-types -T cs_6_5", &shaderByteCode);
			pipelineStateDesc.CS = shaderByteCode;

			pDevice->GetDevice()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pFilterPassPso[2]));
			SetName(m_pFilterPassPso[2], "m_pFilterPassPso[2]");
		}
	}

	void ShadowDenoiser::OnDestroy(void)
	{
		if (m_pPrepareRootSig)
		{
			m_pPrepareRootSig->Release();
			m_pPrepareRootSig = nullptr;
		}

		if (m_pPreparePso)
		{
			m_pPreparePso->Release();
			m_pPreparePso = nullptr;
		}

		if (m_pTileClassificationRootSig)
		{
			m_pTileClassificationRootSig->Release();
			m_pTileClassificationRootSig = nullptr;
		}

		if (m_pTileClassificationPso)
		{
			m_pTileClassificationPso->Release();
			m_pTileClassificationPso = nullptr;
		}

		if (m_pFilterPassRootSig)
		{
			m_pFilterPassRootSig->Release();
			m_pFilterPassRootSig = nullptr;
		}

		if (m_pFilterPassPso[0])
		{
			m_pFilterPassPso[0]->Release();
			m_pFilterPassPso[0] = nullptr;
		}

		if (m_pFilterPassPso[1])
		{
			m_pFilterPassPso[1]->Release();
			m_pFilterPassPso[1] = nullptr;
		}

		if (m_pFilterPassPso[2])
		{
			m_pFilterPassPso[2]->Release();
			m_pFilterPassPso[2] = nullptr;
		}
	}

	void ShadowDenoiser::OnCreateWindowSizeDependentResources(Device* pDevice, uint32_t Width, uint32_t Height)
	{
		m_width = Width;
		m_height = Height;

		uint32_t const xTiles = DivRoundUp(Width, k_tileSizeX);
		uint32_t const yTiles = DivRoundUp(Height, k_tileSizeY);
		// depth history
		{
			CD3DX12_RESOURCE_DESC const desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R32_FLOAT,
				Width,
				Height,
				1, 1, 1, 0,
				D3D12_RESOURCE_FLAG_NONE);
			m_depthHistory.Init(pDevice, "Depth History", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

			m_depthHistory.CreateSRV(4, &m_tileClassificationTable);
		}
		// moments
		{
			CD3DX12_RESOURCE_DESC const desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R11G11B10_FLOAT,
				// DXGI_FORMAT_R16G16B16A16_FLOAT,
				Width,
				Height,
				1, 1, 1, 0,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			m_moments[0].Init(pDevice, "Moments 0", &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr);
			m_moments[1].Init(pDevice, "Moments 1", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

			m_moments[0].CreateSRV(0, &m_momentsTable);
			m_moments[1].CreateSRV(1, &m_momentsTable);
			m_moments[0].CreateUAV(2, &m_momentsTable);
			m_moments[1].CreateUAV(3, &m_momentsTable);
		}
		// scratch
		{
			CD3DX12_RESOURCE_DESC const desc = CD3DX12_RESOURCE_DESC::Tex2D(
				DXGI_FORMAT_R16G16_FLOAT,
				Width,
				Height,
				1, 1, 1, 0,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			m_scratch[0].Init(pDevice, "Scratch 0", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);
			m_scratch[1].Init(pDevice, "Scratch 1", &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr);

			m_scratch[0].CreateSRV(0, &m_scratchTable);
			m_scratch[1].CreateSRV(1, &m_scratchTable);
			m_scratch[0].CreateUAV(2, &m_scratchTable);
			m_scratch[1].CreateUAV(3, &m_scratchTable);

			m_scratch[0].CreateUAV(7, &m_tileClassificationTable);
			m_scratch[1].CreateSRV(3, &m_tileClassificationTable);
		}

		uint32_t const tileCount = xTiles * yTiles;
		// meta data buffer
		{
			m_tileMetaBuffer.InitBuffer(
				pDevice,
				"Tile Meta Data",
				&CD3DX12_RESOURCE_DESC::Buffer(
					sizeof(uint32_t) * tileCount,
					D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
				sizeof(uint32_t),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			m_tileMetaBuffer.CreateBufferUAV(6, nullptr, &m_tileClassificationTable);
			m_tileMetaBuffer.CreateSRV(2, &m_filterPassTable);
		}
		// tile data buffer
		{
			m_tileBuffer.InitBuffer(
				pDevice,
				"Tile Data",
				&CD3DX12_RESOURCE_DESC::Buffer(
					sizeof(uint32_t) * tileCount,
					D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
				sizeof(uint32_t),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			m_tileBuffer.CreateBufferUAV(0, nullptr, &m_prepareTable);
			m_tileBuffer.CreateSRV(5, &m_tileClassificationTable);
		}

		m_momentIndex = 0;
		m_bIsFristFrame = true;
	}

	void ShadowDenoiser::OnDestroyWindowSizeDependentResources(void)
	{
		m_depthHistory.OnDestroy();
		m_moments[0].OnDestroy();
		m_moments[1].OnDestroy();
		m_scratch[0].OnDestroy();
		m_scratch[1].OnDestroy();
		m_tileMetaBuffer.OnDestroy();
		m_tileBuffer.OnDestroy();
	}

	void ShadowDenoiser::BindNormalTexture(Texture& normal)
	{
		normal.CreateSRV(2, &m_tileClassificationTable);
		normal.CreateSRV(1, &m_filterPassTable);
	}

	void ShadowDenoiser::BindDepthTexture(Texture& depth)
	{
		depth.CreateSRV(0, &m_tileClassificationTable);
		depth.CreateSRV(0, &m_filterPassTable);

		m_pDepth = &depth;
	}

	void ShadowDenoiser::BindMotionVectorTexture(Texture& motionVector)
	{
		motionVector.CreateSRV(1, &m_tileClassificationTable);
	}

	void ShadowDenoiser::Denoise(ID3D12GraphicsCommandList* pCommandList, DynamicBufferRing& pDynamicBufferRing, DenoiserControl const& dc, CBV_SRV_UAV& input, CBV_SRV_UAV& output, GPUTimestamps* pGpuTimer)
	{
		UserMarker marker(pCommandList, "Denoise shadows");

		uint32_t const ThreadGroupCountX = DivRoundUp(m_width, k_tileSizeX);
		uint32_t const ThreadGroupCountY = DivRoundUp(m_height, k_tileSizeX);

		// prepare
		{
			UserMarker marker(pCommandList, "prepare");

			{
				D3D12_RESOURCE_BARRIER barrier[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_tileBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				};
				pCommandList->ResourceBarrier(ARRAYSIZE(barrier), barrier);
			}

			// Bind the descriptor heaps and root signature
			pCommandList->SetComputeRootSignature(m_pPrepareRootSig);

			// Bind the pipeline state
			//
			pCommandList->SetPipelineState(m_pPreparePso);

			struct
			{
				uint32_t width;
				uint32_t height;
			} cb = { m_width, m_height };
			D3D12_GPU_VIRTUAL_ADDRESS const cbAddress = pDynamicBufferRing.AllocConstantBuffer(sizeof(cb), &cb);

			// Bind the descriptor set
			//
			pCommandList->SetComputeRootConstantBufferView(0, cbAddress);
			pCommandList->SetComputeRootDescriptorTable(1, input.GetGPU());
			pCommandList->SetComputeRootDescriptorTable(2, m_prepareTable.GetGPU());

			uint32_t const ThreadGroupCountX2 = DivRoundUp(m_width, k_tileSizeX * 4);
			uint32_t const ThreadGroupCountY2 = DivRoundUp(m_height, k_tileSizeY * 4);
			pCommandList->Dispatch(ThreadGroupCountX2, ThreadGroupCountY2, 1);

			pGpuTimer->GetTimeStamp(pCommandList, "Denoiser prepare");
		}

		// tile class
		{
			UserMarker marker(pCommandList, "tile classification");

			{
				D3D12_RESOURCE_BARRIER barrier[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_tileBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_tileMetaBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_scratch[0].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_moments[m_momentIndex].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_moments[m_momentIndex ^ 1].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				};
				pCommandList->ResourceBarrier(ARRAYSIZE(barrier), barrier);
			}

			// Bind the descriptor heaps and root signature
			pCommandList->SetComputeRootSignature(m_pTileClassificationRootSig);

			// Bind the pipeline state
			//
			pCommandList->SetPipelineState(m_pTileClassificationPso);


			struct
			{
				float eye[3];
				int bIsFristFrame;
				int width;
				int height;
				float invWidth;
				float invHeight;
				math::Matrix4 inverseProj;
				math::Matrix4 reproj;
				math::Matrix4 inverseViewProj;
			} cb =
			{
				dc.camPos.getX(), dc.camPos.getY(), dc.camPos.getZ(),
				m_bIsFristFrame,
				(int)m_width, (int)m_height,
				1.f / m_width, 1.f / m_height,
				dc.inverseProj,
				dc.reprojection,
				dc.inverseViewProj

			};
			D3D12_GPU_VIRTUAL_ADDRESS const cbAddress = pDynamicBufferRing.AllocConstantBuffer(sizeof(cb), &cb);

			// Bind the descriptor set
			//
			pCommandList->SetComputeRootConstantBufferView(0, cbAddress);
			pCommandList->SetComputeRootDescriptorTable(1, m_tileClassificationTable.GetGPU());
			pCommandList->SetComputeRootDescriptorTable(2, m_momentsTable.GetGPU(m_momentIndex));
			pCommandList->SetComputeRootDescriptorTable(3, m_momentsTable.GetGPU(m_momentIndex ^ 1 + 2));

			pCommandList->Dispatch(ThreadGroupCountX, ThreadGroupCountY, 1);

			m_momentIndex ^= 1;

			pGpuTimer->GetTimeStamp(pCommandList, "Denoiser tile classification");

		}

		// copy depth, do it here to prim/use the Infinity cache
		{
			UserMarker marker(pCommandList, "copy depth");

			{
				D3D12_RESOURCE_BARRIER barrier[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_depthHistory.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
					CD3DX12_RESOURCE_BARRIER::Transition(m_tileMetaBuffer.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_scratch[1].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_scratch[0].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCommandList->ResourceBarrier(ARRAYSIZE(barrier), barrier);
			}

			pCommandList->CopyResource(m_depthHistory.GetResource(), m_pDepth->GetResource());

			pGpuTimer->GetTimeStamp(pCommandList, "Denoiser depth copy");
		}

		// filter
		{
			UserMarker marker(pCommandList, "filter");

			// Bind the descriptor heaps and root signature
			pCommandList->SetComputeRootSignature(m_pFilterPassRootSig);

			struct
			{
				math::Matrix4 inverseProj;
				int width;
				int height;
				float invWidth;
				float invHeight;

				float depthSimilaritySigma;
				float _pad[3];
			} cb =
			{
				dc.inverseProj,
				(int)m_width, (int)m_height,
				1.f / m_width, 1.f / m_height,
				1.f,
			};
			D3D12_GPU_VIRTUAL_ADDRESS const cbAddress = pDynamicBufferRing.AllocConstantBuffer(sizeof(cb), &cb);

			// Bind the descriptor set
			//
			pCommandList->SetComputeRootConstantBufferView(0, cbAddress);
			pCommandList->SetComputeRootDescriptorTable(1, m_filterPassTable.GetGPU());

			// pass 0
			// Bind the pipeline state
			//
			pCommandList->SetPipelineState(m_pFilterPassPso[0]);

			pCommandList->SetComputeRootDescriptorTable(2, m_scratchTable.GetGPU(0));
			pCommandList->SetComputeRootDescriptorTable(3, m_scratchTable.GetGPU(1 + 2));

			pCommandList->Dispatch(ThreadGroupCountX, ThreadGroupCountY, 1);

			{
				D3D12_RESOURCE_BARRIER barrier[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_scratch[0].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
					CD3DX12_RESOURCE_BARRIER::Transition(m_scratch[1].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCommandList->ResourceBarrier(ARRAYSIZE(barrier), barrier);
			}

			pGpuTimer->GetTimeStamp(pCommandList, "Denoiser filter 1");

			// pass 1
			// Bind the pipeline state
			//
			pCommandList->SetPipelineState(m_pFilterPassPso[1]);

			pCommandList->SetComputeRootDescriptorTable(2, m_scratchTable.GetGPU(1));
			pCommandList->SetComputeRootDescriptorTable(3, m_scratchTable.GetGPU(0 + 2));

			pCommandList->Dispatch(ThreadGroupCountX, ThreadGroupCountY, 1);

			{
				D3D12_RESOURCE_BARRIER barrier[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(m_depthHistory.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(m_scratch[0].GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				};
				pCommandList->ResourceBarrier(ARRAYSIZE(barrier), barrier);
			}

			pGpuTimer->GetTimeStamp(pCommandList, "Denoiser filter 2");

			// pass 2
			// Bind the pipeline state
			//
			pCommandList->SetPipelineState(m_pFilterPassPso[2]);

			pCommandList->SetComputeRootDescriptorTable(2, m_scratchTable.GetGPU(0));
			pCommandList->SetComputeRootDescriptorTable(3, output.GetGPU());

			pCommandList->Dispatch(ThreadGroupCountX, ThreadGroupCountY, 1);

			pGpuTimer->GetTimeStamp(pCommandList, "Denoiser filter 3");

		}

		m_bIsFristFrame = false;

	}

}