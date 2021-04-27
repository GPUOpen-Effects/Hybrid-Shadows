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
#include "Renderer.h"
#include "UI.h"

#include <stdlib.h>

constexpr float GOLDEN_RATIO = 1.6180339887f;

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void Renderer::OnCreate(Device* pDevice, SwapChain *pSwapChain, float fontSize, const UIState* pState)
{
	m_pDevice = pDevice;

	// Initialize helpers

	// Create all the heaps for the resources views
	const uint32_t cbvDescriptorCount = 4000;
	const uint32_t srvDescriptorCount = 8000;
	const uint32_t uavDescriptorCount = 60;
	const uint32_t dsvDescriptorCount = 60;
	const uint32_t rtvDescriptorCount = 60;
	const uint32_t samplerDescriptorCount = 20;
	m_resourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, dsvDescriptorCount, rtvDescriptorCount, samplerDescriptorCount);

	// Create a commandlist ring for the Direct queue
	uint32_t commandListsPerBackBuffer = 8;
	m_CommandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer, pDevice->GetGraphicsQueue()->GetDesc());

	// Create a 'dynamic' constant buffer
	const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
	m_ConstantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, &m_resourceViewHeaps);

	// Create a 'static' pool for vertices, indices and constant buffers
	const uint32_t staticGeometryMemSize = (5 * 128) * 1024 * 1024;
	m_VidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, true, "StaticGeom");

	// initialize the GPU time stamps module
	m_GPUTimer.OnCreate(pDevice, backBufferCount);

    // Quick helper to upload resources, it has it's own commandList and uses suballocation.
    const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
    m_UploadHeap.OnCreate(pDevice, uploadHeapMemSize);    // initialize an upload heap (uses suballocation for faster results)

	// Create GBuffer and render passes
	//
	{
		m_GBuffer.OnCreate(
			pDevice,
			&m_resourceViewHeaps,
			{
				{ GBUFFER_DEPTH, DXGI_FORMAT_D32_FLOAT},
				{ GBUFFER_FORWARD, DXGI_FORMAT_R16G16B16A16_FLOAT},
				{ GBUFFER_MOTION_VECTORS, DXGI_FORMAT_R16G16_FLOAT},
				{ GBUFFER_NORMAL_BUFFER, DXGI_FORMAT_R11G11B10_FLOAT},
			},
			1
			);

		m_renderPassPrePass.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_MOTION_VECTORS | GBUFFER_NORMAL_BUFFER);
		m_renderPassJustDepthAndHdr.OnCreate(&m_GBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD);
	}

	m_customShadowResolve.OnCreate(m_pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing);
	// Create the shadow mask descriptors
	m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMaskUAV);
	m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMaskSRV);

	// Create a Shadowmap atlas to hold 4 cascades/spotlights
	m_resourceViewHeaps.AllocDSVDescriptor(5, &m_ShadowMapDSV);
	m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_ShadowMapSRV);

	m_skyDome.OnCreate(pDevice, &m_UploadHeap, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, "..\\media\\cauldron-media\\envmaps\\papermill\\diffuse.dds", "..\\media\\cauldron-media\\Brutalism\\Cubemap_layered_half.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_skyDomeProc.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_wireframe.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
	m_wireframeBox.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool);
	m_downSample.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_bloom.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_TAA.OnCreate(pDevice, &m_resourceViewHeaps, &m_VidMemBufferPool);
	m_magnifierPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);

	// Create tonemapping pass
	m_toneMappingPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());
	m_toneMappingCS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing);
	m_colorConversionPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_ConstantBufferRing, &m_VidMemBufferPool, pSwapChain->GetFormat());

	// Initialize UI rendering resources
	m_ImGUI.OnCreate(pDevice, &m_UploadHeap, &m_resourceViewHeaps, &m_ConstantBufferRing, pSwapChain->GetFormat(), fontSize);


	m_blueNoise = CreateBlueNoiseTexture(m_pDevice, m_UploadHeap);

	m_asFactory.OnCreate(m_pDevice);
	m_scratchBuffer.OnCreate(m_pDevice, 128 * 1024 * 1024, true, "AS Scratch buffer");

	m_shadowTrace.OnCreate(m_pDevice, &m_resourceViewHeaps);
	m_shadowTrace.SetBlueNoise(m_blueNoise);

	OnResizeShadowMapWidth(pState);

	// Make sure upload heap has finished uploading before continuing
	m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());
	m_UploadHeap.FlushAndFinish();

	m_frame = 0;
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void Renderer::OnDestroy()
{
	m_asyncPool.Flush();

	m_shadowMap.OnDestroy();
	m_ImGUI.OnDestroy();
	m_colorConversionPS.OnDestroy();
	m_toneMappingCS.OnDestroy();
	m_toneMappingPS.OnDestroy();
	m_TAA.OnDestroy();
	m_bloom.OnDestroy();
	m_downSample.OnDestroy();
	m_magnifierPS.OnDestroy();
	m_wireframeBox.OnDestroy();
	m_wireframe.OnDestroy();
	m_skyDomeProc.OnDestroy();
	m_skyDome.OnDestroy();
	m_customShadowResolve.OnDestroy();
	m_GBuffer.OnDestroy();
	m_UploadHeap.OnDestroy();
	m_GPUTimer.OnDestroy();
	m_VidMemBufferPool.OnDestroy();
	m_ConstantBufferRing.OnDestroy();
	m_resourceViewHeaps.OnDestroy();
	m_CommandListRing.OnDestroy();

	m_blueNoise.OnDestroy();

	m_asFactory.OnDestroy();
	m_scratchBuffer.OnDestroy();

	m_shadowTrace.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnCreateWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void Renderer::OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height, const UIState* pState)
{
	m_Width = Width;
	m_Height = Height;

	// Set the viewport & scissors rect
	m_viewport = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
	m_rectScissor = { 0, 0, (LONG)Width, (LONG)Height };

	// Create shadow mask
	//
	m_ShadowMask.Init(m_pDevice, "shadowbuffer", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
	m_ShadowMask.CreateUAV(0, &m_ShadowMaskUAV);
	m_ShadowMask.CreateSRV(0, &m_ShadowMaskSRV);

	// Create GBuffer
	//
	m_GBuffer.OnCreateWindowSizeDependentResources(pSwapChain, Width, Height);
	m_renderPassPrePass.OnCreateWindowSizeDependentResources(Width, Height);
	m_renderPassJustDepthAndHdr.OnCreateWindowSizeDependentResources(Width, Height);

	m_TAA.OnCreateWindowSizeDependentResources(Width, Height, &m_GBuffer);

	// update bloom and downscaling effect
	//
	m_downSample.OnCreateWindowSizeDependentResources(m_Width, m_Height, &m_GBuffer.m_HDR, 5); //downsample the HDR texture 5 times
	m_bloom.OnCreateWindowSizeDependentResources(m_Width / 2, m_Height / 2, m_downSample.GetTexture(), 5, &m_GBuffer.m_HDR);
	m_magnifierPS.OnCreateWindowSizeDependentResources(&m_GBuffer.m_HDR);

	m_shadowTrace.OnCreateWindowSizeDependentResources(m_pDevice, Width, Height);
	m_shadowTrace.BindDepthTexture(m_GBuffer.m_DepthBuffer);
	m_shadowTrace.BindNormalTexture(m_GBuffer.m_NormalBuffer);
	m_shadowTrace.BindMotionVectorTexture(m_GBuffer.m_MotionVectors);

	// Update pipelines in case the format of the RTs changed (this happens when going HDR)
	m_colorConversionPS.UpdatePipelines(pSwapChain->GetFormat(), pSwapChain->GetDisplayMode());
	m_toneMappingPS.UpdatePipelines(pSwapChain->GetFormat());
	m_ImGUI.UpdatePipeline((pSwapChain->GetDisplayMode() == DISPLAYMODE_SDR) ? pSwapChain->GetFormat() : m_GBuffer.m_HDR.GetFormat());
}

//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void Renderer::OnDestroyWindowSizeDependentResources()
{
	m_bloom.OnDestroyWindowSizeDependentResources();
	m_downSample.OnDestroyWindowSizeDependentResources();

	m_GBuffer.OnDestroyWindowSizeDependentResources();

	m_TAA.OnDestroyWindowSizeDependentResources();

	m_magnifierPS.OnDestroyWindowSizeDependentResources();

	m_ShadowMask.OnDestroy();

	m_shadowTrace.OnDestroyWindowSizeDependentResources();
}

void Renderer::OnUpdateDisplayDependentResources(SwapChain* pSwapChain)
{
	// Update pipelines in case the format of the RTs changed (this happens when going HDR)
	m_colorConversionPS.UpdatePipelines(pSwapChain->GetFormat(), pSwapChain->GetDisplayMode());
	m_toneMappingPS.UpdatePipelines(pSwapChain->GetFormat());
	m_ImGUI.UpdatePipeline((pSwapChain->GetDisplayMode() == DISPLAYMODE_SDR) ? pSwapChain->GetFormat() : m_GBuffer.m_HDR.GetFormat());
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int Renderer::LoadScene(GLTFCommon* pGLTFCommon, int stage)
{
	// show loading progress
	//
	ImGui::OpenPopup("Loading");
	if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		float progress = (float)stage / 13.0f;
		ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
		ImGui::EndPopup();
	}

	// use multithreading
	AsyncPool* pAsyncPool = &m_asyncPool;

	// Loading stages
	//
	if (stage == 0)
	{
	}
	else if (stage == 3)
	{
		Profile p("m_pGltfLoader->Load");

		m_pGLTFTexturesAndBuffers = new GLTFTexturesAndBuffers();
		m_pGLTFTexturesAndBuffers->OnCreate(m_pDevice, pGLTFCommon, &m_UploadHeap, &m_VidMemBufferPool, &m_ConstantBufferRing);
	}
	else if (stage == 4)
	{
		Profile p("LoadTextures");

		// here we are loading onto the GPU all the textures and the inverse matrices
		// this data will be used to create the PBR and Depth passes
		m_pGLTFTexturesAndBuffers->LoadTextures(pAsyncPool);

		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());

		m_UploadHeap.FlushAndFinish();
	}
	else if (stage == 5)
	{
		Profile p("BLAS build");

		m_asFactory.BuildFromGltf(m_pDevice, m_pGLTFTexturesAndBuffers, &m_resourceViewHeaps, &m_UploadHeap);
		m_shadowTrace.SetUVBuffer(*m_asFactory.GetUVBuffer());

		ID3D12GraphicsCommandList* pCmdLst1 = m_CommandListRing.GetNewCommandList();

		for (auto&& blas : m_asFactory.GetBLASVector())
		{
			blas.Build(pCmdLst1, m_scratchBuffer);
		}

		ThrowIfFailed(pCmdLst1->Close());
		ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
		m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

		m_UploadHeap.FlushAndFinish();
	}
	else if (stage == 6)
	{
		Profile p("m_gltfMotionVector->OnCreate");

		//create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
		m_gltfMotionVector = new GltfMotionVectorsPass();
		m_gltfMotionVector->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_resourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			m_GBuffer.m_MotionVectors.GetFormat(),
			m_GBuffer.m_NormalBuffer.GetFormat(),
			pAsyncPool
		);
	}
	else if (stage == 7)
	{
		Profile p("m_gltfDepth->OnCreate");

		//create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
		m_gltfDepth = new GltfDepthPass();
		m_gltfDepth->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_resourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			pAsyncPool,
			DXGI_FORMAT_D16_UNORM
		);
	}
	else if (stage == 9)
	{
		Profile p("m_gltfPBR->OnCreate");

		// same thing as above but for the PBR pass
		m_gltfPBR = new GltfPbrPass();
		m_gltfPBR->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_resourceViewHeaps,
			&m_ConstantBufferRing,
			m_pGLTFTexturesAndBuffers,
			&m_skyDome,
			false,                  // use a SSAO mask
			true,
			&m_renderPassJustDepthAndHdr,
			pAsyncPool
		);

	}
	else if (stage == 10)
	{
		Profile p("m_gltfBBox->OnCreate");

		// just a bounding box pass that will draw boundingboxes instead of the geometry itself
		m_gltfBBox = new GltfBBoxPass();
		m_gltfBBox->OnCreate(
			m_pDevice,
			&m_UploadHeap,
			&m_resourceViewHeaps,
			&m_ConstantBufferRing,
			&m_VidMemBufferPool,
			m_pGLTFTexturesAndBuffers,
			&m_wireframe
		);

		// we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
		m_VidMemBufferPool.UploadData(m_UploadHeap.GetCommandList());

	}
	else if (stage == 11)
	{
		Profile p("Flush");

		m_UploadHeap.FlushAndFinish();

		//once everything is uploaded we dont need he upload heaps anymore
		m_VidMemBufferPool.FreeUploadHeap();

		// tell caller that we are done loading the map
		return 0;
	}

	stage++;
	return stage;

}

//--------------------------------------------------------------------------------------
//
// UnloadScene
//
//--------------------------------------------------------------------------------------
void Renderer::UnloadScene()
{
	// wait for all the async loading operations to finish
	m_asyncPool.Flush();
	m_pDevice->GPUFlush();

	if (m_gltfPBR)
	{
		m_gltfPBR->OnDestroy();
		delete m_gltfPBR;
		m_gltfPBR = NULL;
	}

	if (m_gltfDepth)
	{
		m_gltfDepth->OnDestroy();
		delete m_gltfDepth;
		m_gltfDepth = NULL;
	}

	if (m_gltfMotionVector)
	{
		m_gltfMotionVector->OnDestroy();
		delete m_gltfMotionVector;
		m_gltfMotionVector = NULL;
	}

	if (m_gltfBBox)
	{
		m_gltfBBox->OnDestroy();
		delete m_gltfBBox;
		m_gltfBBox = NULL;
	}

	if (m_pGLTFTexturesAndBuffers)
	{
		m_pGLTFTexturesAndBuffers->OnDestroy();
		delete m_pGLTFTexturesAndBuffers;
		m_pGLTFTexturesAndBuffers = NULL;
	}

	m_asFactory.ClearBuiltStructures();
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void Renderer::OnRender(const UIState* pState, const Camera& cam, SwapChain* pSwapChain)
{
	// Timing values
	UINT64 gpuTicksPerSecond;
	m_pDevice->GetGraphicsQueue()->GetTimestampFrequency(&gpuTicksPerSecond);

	// Let our resource managers do some house keeping
	m_CommandListRing.OnBeginFrame();
	m_ConstantBufferRing.OnBeginFrame();
	m_GPUTimer.OnBeginFrame(gpuTicksPerSecond, &m_TimeStamps);

	// Sets the perFrame data 
	per_frame* pPerFrame = NULL;
	if (m_pGLTFTexturesAndBuffers)
	{
		// fill as much as possible using the GLTF (camera, lights, ...)
		pPerFrame = m_pGLTFTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(cam);

		// Set some lighting factors
		pPerFrame->iblFactor = pState->IBLFactor;
		pPerFrame->emmisiveFactor = pState->EmissiveFactor;
		pPerFrame->invScreenResolution[0] = 1.0f / ((float)m_Width);
		pPerFrame->invScreenResolution[1] = 1.0f / ((float)m_Height);

		pPerFrame->wireframeOptions.setX(pState->WireframeColor[0]);
		pPerFrame->wireframeOptions.setY(pState->WireframeColor[1]);
		pPerFrame->wireframeOptions.setZ(pState->WireframeColor[2]);
		pPerFrame->wireframeOptions.setW(pState->WireframeMode == UIState::WireframeMode::WIREFRAME_MODE_SOLID_COLOR ? 1.0f : 0.0f);

		m_pGLTFTexturesAndBuffers->SetPerFrameConstants();
		m_pGLTFTexturesAndBuffers->SetSkinningMatricesForSkeletons();
	}

	Raytracing::ClassifyMethod classifyMethod = Raytracing::ClassifyMethod::ByNormals;
	bool bNeedCascades = false;
	bool bNeedShadowResolve = false;
	bool bNeedRt = true;
	switch (pState->hMode)
	{
	case RtHybridMode::CascadesOnly:
		bNeedRt = false;
		bNeedCascades = true;
		bNeedShadowResolve = true;
		break;
	case RtHybridMode::RaytracingOnly:
		classifyMethod = Raytracing::ClassifyMethod::ByNormals;
		break;
	case RtHybridMode::HybridRaytracing:
		classifyMethod = Raytracing::ClassifyMethod::ByCascades;
		bNeedCascades = true;
		break;
	default:
		break;
	}

	// command buffer calls
	//
	ID3D12GraphicsCommandList* pCmdLst1 = m_CommandListRing.GetNewCommandList();
	ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_resourceViewHeaps.GetCBV_SRV_UAVHeap(), m_resourceViewHeaps.GetSamplerHeap() };
	pCmdLst1->SetDescriptorHeaps(2, pDescriptorHeaps);

	m_GPUTimer.GetTimeStamp(pCmdLst1, "Begin Frame");

	pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Depth + Normal + Motion Vector PrePass  ------------------------------------------
	//
	if (m_gltfMotionVector && pPerFrame != NULL)
	{
		UserMarker marker(pCmdLst1, "Depth + Normal + Motion Vector PrePass");
		pCmdLst1->RSSetViewports(1, &m_viewport);
		pCmdLst1->RSSetScissorRects(1, &m_rectScissor);
		m_renderPassPrePass.BeginPass(pCmdLst1, true);

		GltfMotionVectorsPass::per_frame* cbDepthPerFrame = m_gltfMotionVector->SetPerFrameConstants();
		cbDepthPerFrame->mCurrViewProj = pPerFrame->mCameraCurrViewProj;
		cbDepthPerFrame->mPrevViewProj = pPerFrame->mCameraPrevViewProj;
		m_gltfMotionVector->Draw(pCmdLst1);

		m_renderPassPrePass.EndPass();
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Depth + Normal + Motion Vector PrePass");
	}

	{
		D3D12_RESOURCE_BARRIER barrier[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(barrier), barrier);
	}

	// Find directional light
	Light* directionalLightptr = NULL;
	if (pPerFrame != NULL)
	{
		for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
		{
			if (pPerFrame->lights[i].type != LightType_Directional)
				continue;

			directionalLightptr = &pPerFrame->lights[i];
			break;
		}
	}

	// Render shadow maps
	if (m_gltfDepth && pPerFrame != NULL && bNeedCascades)
	{
		pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
		UserMarker marker(pCmdLst1, "Shadow Cascade Pass");
		pCmdLst1->ClearDepthStencilView(m_ShadowMapDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Clear shadow map cascades");


		m_CSMManager.OnCreate(pState->numCascades);

		m_CSMManager.SetupCascades(cam.GetProjection(), cam.GetView(),
			directionalLightptr->mLightView, cam.GetNearPlane(), m_pGLTFTexturesAndBuffers->m_pGLTFCommon, pState->numCascades,
			pState->cascadeSplitPoint, pState->cascadeType, static_cast<float>(m_shadowMap.GetWidth()),
			pState->bMoveLightTexelSize);

		// Scene MUST HAVE directional light
		assert(directionalLightptr != nullptr);

		std::vector<math::Matrix4> matShadowProj = m_CSMManager.GetShadowProj();

		for (int i = 0; i < pState->numCascades; ++i)
		{
			if (pState->cascadeSkipIndexes[i]) continue;

			pCmdLst1->OMSetRenderTargets(0, nullptr, false, &m_ShadowMapDSV.GetCPU(i + 1));
			pCmdLst1->RSSetViewports(1, &m_shadowViewport);
			pCmdLst1->RSSetScissorRects(1, &m_shadowRectScissor);

			GltfDepthPass::per_frame* cbDepthPerFrame = m_gltfDepth->SetPerFrameConstants(i + 1);

			cbDepthPerFrame->mViewProj = matShadowProj[i] * directionalLightptr->mLightView;

			std::string pass = "Shadow Cascade Pass" + std::to_string(i);
			UserMarker marker(pCmdLst1, pass.c_str());

			m_gltfDepth->Draw(pCmdLst1, i + 1);

			m_GPUTimer.GetTimeStamp(pCmdLst1, pass.c_str());
		}
		pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ));
	}


	// Shadow resolve ---------------------------------------------------------------------------
	//
	if (m_gltfDepth && pPerFrame != NULL && bNeedShadowResolve)
	{
		const D3D12_RESOURCE_BARRIER preShadowResolve[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(preShadowResolve), preShadowResolve);

		CustomShadowResolveFrame shadowResolveFrame;
		shadowResolveFrame.m_Width = m_Width;
		shadowResolveFrame.m_Height = m_Height;
		shadowResolveFrame.m_ShadowMapSRV = m_ShadowMapSRV;
		shadowResolveFrame.m_DepthBufferSRV = m_GBuffer.m_DepthBufferSRV;
		shadowResolveFrame.m_ShadowBufferUAV = m_ShadowMaskUAV;

		CustomShadowResolvePass::per_frame* cbShadowResolvePerFrame = m_customShadowResolve.SetPerFrameConstants();
		cbShadowResolvePerFrame->m_mInverseCameraCurrViewProj = pPerFrame->mInverseCameraCurrViewProj;
		cbShadowResolvePerFrame->m_mLightView = directionalLightptr->mLightView;
		cbShadowResolvePerFrame->m_fCascadeBlendArea = pState->blurBetweenCascadesAmount;
		cbShadowResolvePerFrame->m_fShadowBiasFromGUI = pState->pcfOffset;
		cbShadowResolvePerFrame->m_nTextureSizeX = m_Width;
		cbShadowResolvePerFrame->m_nTextureSizeY = m_Height;
		math::Matrix4 matTextureScale = math::Matrix4::scale(math::Vector3(0.5f, -0.5f, 1.0f));
		math::Matrix4 matTextureTranslation = math::Matrix4::translation(math::Vector3(.5f, .5f, 0.f));
		std::vector<math::Matrix4> matShadowProj = m_CSMManager.GetShadowProj();
		for (int shadowMapCascadeIndex = 0; shadowMapCascadeIndex < pState->numCascades; ++shadowMapCascadeIndex)
		{
			math::Matrix4 mShadowTexture = matTextureTranslation * matTextureScale * matShadowProj[shadowMapCascadeIndex];
			cbShadowResolvePerFrame->m_vCascadeScale[shadowMapCascadeIndex] =
				math::Vector4(mShadowTexture.getCol0().getX(), mShadowTexture.getCol1().getY(), mShadowTexture.getCol2().getZ(), 1.0f);
			cbShadowResolvePerFrame->m_vCascadeOffset[shadowMapCascadeIndex] =
				math::Vector4(mShadowTexture.getCol3().getX(), mShadowTexture.getCol3().getY(), mShadowTexture.getCol3().getZ(), 0.0f);
		}

		cbShadowResolvePerFrame->m_fMaxBorderPadding = ((float)pState->shadowMapWidth - 1.0f) /
			(float)pState->shadowMapWidth;
		cbShadowResolvePerFrame->m_fMinBorderPadding = 1.0f /
			(float)pState->shadowMapWidth;
		cbShadowResolvePerFrame->m_nCascadeLevels = pState->numCascades;
		cbShadowResolvePerFrame->m_fSunSize = tanf(0.5f * pState->sunSizeAngle);
		cbShadowResolvePerFrame->m_fLightDir[0] = -directionalLightptr->direction[0];
		cbShadowResolvePerFrame->m_fLightDir[1] = -directionalLightptr->direction[1];
		cbShadowResolvePerFrame->m_fLightDir[2] = -directionalLightptr->direction[2];

		m_customShadowResolve.Draw(pCmdLst1, m_pGLTFTexturesAndBuffers, &shadowResolveFrame, 0);


		const D3D12_RESOURCE_BARRIER postShadowResolve[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(postShadowResolve), postShadowResolve);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Shadow resolve");
	}

	// raytracing
	if (pPerFrame != NULL && m_gltfDepth && bNeedRt)
	{
		Raytracing::TraceMethod method = Raytracing::TraceMethod::ForceOpaque;
		bool bGatherNonOpaque = false;
		switch (pState->amMode)
		{
		case RtAlphaMaskMode::SkipMaskedObjs:
			method = Raytracing::TraceMethod::ForceOpaque;
			break;
		case RtAlphaMaskMode::ForceMasksOff:
			method = Raytracing::TraceMethod::ForceOpaque;
			bGatherNonOpaque = true;
			break;
		case RtAlphaMaskMode::Mixed:
			method = Raytracing::TraceMethod::MixedTlas;
			bGatherNonOpaque = true;
			break;
		default:
			break;
		}

		m_asFactory.ResetTLAS();
		Raytracing::TLAS tlas0 = m_asFactory.BuildTLASFromGLTF(m_pDevice, m_pGLTFTexturesAndBuffers, true, bGatherNonOpaque);
		Raytracing::TLAS tlas1 = m_asFactory.BuildTLASFromGLTF(m_pDevice, m_pGLTFTexturesAndBuffers, false, true);
		tlas0.Build(pCmdLst1, m_scratchBuffer, m_ConstantBufferRing);
		if (method == Raytracing::TraceMethod::SplitTlas)
		{
			tlas1.Build(pCmdLst1, m_scratchBuffer, m_ConstantBufferRing);
		}
		m_asFactory.SyncTLASBuilds(pCmdLst1);

		m_GPUTimer.GetTimeStamp(pCmdLst1, "Build TLAS");

		Raytracing::TraceControls tc = {};
		tc.sunSize = tanf(0.5f * pState->sunSizeAngle);
		tc.noisePhase = (m_frame & 0xff) * GOLDEN_RATIO; // use golden ratio to animiate noise 
		tc.bRejectLitPixels = pState->bRejectLitPixels;
		// only vaild for hybrid mode
		tc.bUseCascadesForRayT = pState->bUseCascadesForRayT && (pState->hMode == RtHybridMode::HybridRaytracing);

		tc.tileTolerance = pState->tileCutoff;
		tc.cascadeCount = pState->numCascades;
		tc.activeCascades = 0x0;
		for (int i = 0; i < pState->numCascades; ++i)
		{
			tc.activeCascades |= pState->cascadeSkipIndexes[i] << i;
		}
		tc.cascadePixelSize = 1.f / pState->shadowMapWidth;
		tc.cascadeSize = static_cast<float>(pState->shadowMapWidth);
		tc.blockerOffset = pState->pcfOffset;

		tc.lightView = directionalLightptr->mLightView;
		math::Matrix4 const matTextureScale = math::Matrix4::scale(math::Vector3(0.5f, -0.5f, 1.0f));
		math::Matrix4 const matTextureTranslation = math::Matrix4::translation(math::Vector3(.5f, .5f, 0.f));
		std::vector<math::Matrix4> const matShadowProj = m_CSMManager.GetShadowProj();
		const std::vector<float> cascadePartitionsFrustum = m_CSMManager.GetCascadePartitionsFrustum();
		for (int index = 0; index < pState->numCascades; ++index)
		{
			math::Matrix4 mShadowTexture = matTextureTranslation * matTextureScale * matShadowProj[index];
			tc.cascadeScale[index] =
				math::Vector4(mShadowTexture.getCol0().getX(), mShadowTexture.getCol1().getY(), mShadowTexture.getCol2().getZ(), 1.0f);
			tc.cascadeOffset[index] =
				math::Vector4(mShadowTexture.getCol3().getX(), mShadowTexture.getCol3().getY(), mShadowTexture.getCol3().getZ(), 0.0f);
		}
		D3D12_GPU_VIRTUAL_ADDRESS tcAddress = m_shadowTrace.BuildTraceControls(m_ConstantBufferRing, *directionalLightptr, pPerFrame->mInverseCameraCurrViewProj, tc);

		m_shadowTrace.Classify(pCmdLst1, classifyMethod, tcAddress);

		m_GPUTimer.GetTimeStamp(pCmdLst1, "Classify tiles");

		m_shadowTrace.Trace(pCmdLst1, tlas0, tlas1, m_asFactory.GetMaskTextureTable(), method, tcAddress);

		m_GPUTimer.GetTimeStamp(pCmdLst1, "Trace shadows");

		const D3D12_RESOURCE_BARRIER preShadowResolve[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(preShadowResolve), preShadowResolve);

		if (pState->bUseDenoiser)
		{
			m_shadowTrace.DenoiseHitsToShadowMask(pCmdLst1, m_ConstantBufferRing, cam, m_ShadowMaskUAV, &m_GPUTimer);
		}
		else
		{
			if (classifyMethod == Raytracing::ClassifyMethod::ByCascadeRange)
			{
				m_shadowTrace.BlendHitsToShadowMask(pCmdLst1, m_ShadowMaskUAV);
			}
			else
			{
				m_shadowTrace.ResolveHitsToShadowMask(pCmdLst1, m_ShadowMaskUAV);
			}
			m_GPUTimer.GetTimeStamp(pCmdLst1, "Resolve ray hits");
		}

		const D3D12_RESOURCE_BARRIER postShadowResolve[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_ShadowMask.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(postShadowResolve), postShadowResolve);
	}

	{
		D3D12_RESOURCE_BARRIER barrier[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_DepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_NormalBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		};
		pCmdLst1->ResourceBarrier(ARRAYSIZE(barrier), barrier);
	}

	// Render Scene to the GBuffer ------------------------------------------------
	//
	if (pPerFrame != NULL)
	{
		pCmdLst1->RSSetViewports(1, &m_viewport);
		pCmdLst1->RSSetScissorRects(1, &m_rectScissor);

		if (m_gltfPBR)
		{
			const bool bWireframe = pState->WireframeMode != UIState::WireframeMode::WIREFRAME_MODE_OFF;

			std::vector<GltfPbrPass::BatchList> opaque, transparent;
			m_gltfPBR->BuildBatchLists(&opaque, &transparent, bWireframe);

			{
				m_renderPassJustDepthAndHdr.BeginPass(pCmdLst1, false);

				float const clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
				pCmdLst1->ClearRenderTargetView(m_GBuffer.m_HDRRTV.GetCPU(), clearColor, 0, nullptr);
			}

			// Render opaque geometry
			// 
			{
				m_gltfPBR->DrawBatchList(pCmdLst1, &m_ShadowMaskSRV, &opaque, bWireframe);

				m_GPUTimer.GetTimeStamp(pCmdLst1, "PBR Opaque");
			}

            // draw skydome
            {
                m_renderPassJustDepthAndHdr.BeginPass(pCmdLst1, false);

                // Render skydome
                if (pState->SelectedSkydomeTypeIndex == 1)
                {
                    math::Matrix4 clipToView = math::inverse(pPerFrame->mCameraCurrViewProj);
                    m_skyDome.Draw(pCmdLst1, clipToView);
                    m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome cube");
                }
                else if (pState->SelectedSkydomeTypeIndex == 0)
                {
                    SkyDomeProc::Constants skyDomeConstants;
                    skyDomeConstants.invViewProj = math::inverse(pPerFrame->mCameraCurrViewProj);
                    skyDomeConstants.vSunDirection = math::Vector4(1.0f, 0.05f, 0.0f, 0.0f);
                    skyDomeConstants.turbidity = 10.0f;
                    skyDomeConstants.rayleigh = 2.0f;
                    skyDomeConstants.mieCoefficient = 0.005f;
                    skyDomeConstants.mieDirectionalG = 0.8f;
                    skyDomeConstants.luminance = 1.0f;
                    m_skyDomeProc.Draw(pCmdLst1, skyDomeConstants);

					m_GPUTimer.GetTimeStamp(pCmdLst1, "Skydome proc");
				}
			}

			// draw transparent geometry
			//
			{
				std::sort(transparent.begin(), transparent.end());
				m_gltfPBR->DrawBatchList(pCmdLst1, &m_ShadowMaskSRV, &transparent, bWireframe);
				m_GPUTimer.GetTimeStamp(pCmdLst1, "PBR Transparent");
			}
			m_renderPassJustDepthAndHdr.EndPass();
		}

		// draw object's bounding boxes
		//
		if (m_gltfBBox && pPerFrame != NULL)
		{
			if (pState->bDrawBoundingBoxes)
			{
				m_gltfBBox->Draw(pCmdLst1, pPerFrame->mCameraCurrViewProj);

				m_GPUTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
			}
		}

		// draw light's frustums
		//
		if (pState->bDrawLightFrustum && pPerFrame != NULL)
		{
			UserMarker marker(pCmdLst1, "light frustrums");

			math::Vector4 vCenter = math::Vector4(0.0f, 0.0f, 0.5f, 0.0f);
			math::Vector4 vRadius = math::Vector4(1.0f, 1.0f, 0.5f, 0.0f);
			math::Vector4 vColor = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
			{
				math::Matrix4 spotlightMatrix = math::inverse(pPerFrame->lights[i].mLightViewProj);
				math::Matrix4 worldMatrix = pPerFrame->mCameraCurrViewProj * spotlightMatrix;
				m_wireframeBox.Draw(pCmdLst1, &m_wireframe, worldMatrix, vCenter, vRadius, vColor);
			}

			m_GPUTimer.GetTimeStamp(pCmdLst1, "Light's frustum");
		}
	}

	D3D12_RESOURCE_BARRIER preResolve[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
	};
	pCmdLst1->ResourceBarrier(1, preResolve);

	// Post proc---------------------------------------------------------------------------

	// Bloom, takes HDR as input and applies bloom to it.
	{
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { m_GBuffer.m_HDRRTV.GetCPU() };
		pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

        m_downSample.Draw(pCmdLst1);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Downsample");

        m_bloom.Draw(pCmdLst1, &m_GBuffer.m_HDR);
        m_GPUTimer.GetTimeStamp(pCmdLst1, "Bloom");
    }

	// Apply TAA & Sharpen to m_HDR
	if (pState->bUseTAA)
	{
		m_TAA.Draw(pCmdLst1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "TAA");
	}

	// Magnifier Pass: m_HDR as input, pass' own output
	if (pState->bUseMagnifier)
	{
		// Note: assumes m_GBuffer.HDR is in D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		m_magnifierPS.Draw(pCmdLst1, pState->MagnifierParams, m_GBuffer.m_HDRSRV);
		m_GPUTimer.GetTimeStamp(pCmdLst1, "Magnifier");

		// Transition magnifier state to PIXEL_SHADER_RESOURCE, as it is going to be pRscCurrentInput replacing m_GBuffer.m_HDR which is in that state.
		pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_magnifierPS.GetPassOutputResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}


	if (pState->debugMode > 0)
	{
		D3D12_RESOURCE_BARRIER preDebug[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		pCmdLst1->ResourceBarrier(1, preDebug);

		if (pState->debugMode >= 2)
		{
			m_shadowTrace.DebugTileClassification(pCmdLst1, pState->debugMode - 2, m_GBuffer.m_HDRUAV);
		}
		else if (pState->debugMode == 1)
		{
			m_shadowTrace.ResolveHitsToShadowMask(pCmdLst1, m_GBuffer.m_HDRUAV);
		}

		D3D12_RESOURCE_BARRIER postDebug[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
		};
		pCmdLst1->ResourceBarrier(1, postDebug);

		m_GPUTimer.GetTimeStamp(pCmdLst1, "RenderDebug");
	}

	// Start tracking input/output resources at this point to handle HDR and SDR render paths 
	ID3D12Resource* pRscCurrentInput = pState->bUseMagnifier ? m_magnifierPS.GetPassOutputResource() : m_GBuffer.m_HDR.GetResource();
	CBV_SRV_UAV                  SRVCurrentInput = pState->bUseMagnifier ? m_magnifierPS.GetPassOutputSRV() : m_GBuffer.m_HDRSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE  RTVCurrentOutput = pState->bUseMagnifier ? m_magnifierPS.GetPassOutputRTV().GetCPU() : m_GBuffer.m_HDRRTV.GetCPU();
	CBV_SRV_UAV                  UAVCurrentOutput = pState->bUseMagnifier ? m_magnifierPS.GetPassOutputUAV() : m_GBuffer.m_HDRUAV;


	// If using FreeSync HDR we need to to the tonemapping in-place and then apply the GUI, later we'll apply the color conversion into the swapchain
	const bool bHDR = pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR;
	if (bHDR)
	{
		// In place Tonemapping ------------------------------------------------------------------------
		{
			D3D12_RESOURCE_BARRIER inputRscToUAV = CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			pCmdLst1->ResourceBarrier(1, &inputRscToUAV);

			m_toneMappingCS.Draw(pCmdLst1, &UAVCurrentOutput, pState->Exposure, pState->SelectedTonemapperIndex, m_Width, m_Height);

			D3D12_RESOURCE_BARRIER inputRscToRTV = CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
			pCmdLst1->ResourceBarrier(1, &inputRscToRTV);
		}

		// Render HUD  ------------------------------------------------------------------------
		{
			pCmdLst1->RSSetViewports(1, &m_viewport);
			pCmdLst1->RSSetScissorRects(1, &m_rectScissor);
			pCmdLst1->OMSetRenderTargets(1, &RTVCurrentOutput, true, NULL);

			m_ImGUI.Draw(pCmdLst1);

			D3D12_RESOURCE_BARRIER hdrToSRV = CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			pCmdLst1->ResourceBarrier(1, &hdrToSRV);

			m_GPUTimer.GetTimeStamp(pCmdLst1, "ImGUI Rendering");
		}
	}

	// submit command buffer #1
	ThrowIfFailed(pCmdLst1->Close());
	ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

	// Wait for swapchain (we are going to render to it) -----------------------------------
	pSwapChain->WaitForSwapChain();

	// Keep tracking input/output resource views 
	pRscCurrentInput = pState->bUseMagnifier ? m_magnifierPS.GetPassOutputResource() : m_GBuffer.m_HDR.GetResource(); // these haven't changed, re-assign as sanity check
	SRVCurrentInput = pState->bUseMagnifier ? m_magnifierPS.GetPassOutputSRV() : m_GBuffer.m_HDRSRV;            // these haven't changed, re-assign as sanity check
	RTVCurrentOutput = *pSwapChain->GetCurrentBackBufferRTV();
	UAVCurrentOutput = {}; // no BackBufferUAV.


	ID3D12GraphicsCommandList* pCmdLst2 = m_CommandListRing.GetNewCommandList();

	pCmdLst2->RSSetViewports(1, &m_viewport);
	pCmdLst2->RSSetScissorRects(1, &m_rectScissor);
	pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), true, NULL);

	if (bHDR)
	{
		// FS HDR mode! Apply color conversion now.
		//
		m_colorConversionPS.Draw(pCmdLst2, &SRVCurrentInput);
		m_GPUTimer.GetTimeStamp(pCmdLst2, "Color conversion");

		pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
	}
	else
	{
		// non FreeSync HDR mode, that is SDR, here we apply the tonemapping from the HDR into the swapchain and then we render the GUI

		// Tonemapping ------------------------------------------------------------------------
		{
			m_toneMappingPS.Draw(pCmdLst2, &SRVCurrentInput, pState->Exposure, pState->SelectedTonemapperIndex);
			m_GPUTimer.GetTimeStamp(pCmdLst2, "Tone mapping");

			pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pRscCurrentInput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}

		// Render HUD  ------------------------------------------------------------------------
		{
			m_ImGUI.Draw(pCmdLst2);
			m_GPUTimer.GetTimeStamp(pCmdLst2, "ImGUI Rendering");
		}
	}

	// If magnifier is used, make sure m_GBuffer.m_HDR which is not pRscCurrentInput gets reverted back to RT state.
	if (pState->bUseMagnifier)
		pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_GBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));

	if (!m_pScreenShotName.empty())
	{
		m_saveTexture.CopyRenderTargetIntoStagingTexture(m_pDevice->GetDevice(), pCmdLst2, pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	// Transition swapchain into present mode
	pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	m_GPUTimer.OnEndFrame();

	m_GPUTimer.CollectTimings(pCmdLst2);

	// Close & Submit the command list #2 -------------------------------------------------
	ThrowIfFailed(pCmdLst2->Close());

	ID3D12CommandList* CmdListList2[] = { pCmdLst2 };
	m_pDevice->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);

	// Handle screenshot request
	if (!m_pScreenShotName.empty())
	{
		m_saveTexture.SaveStagingTextureAsJpeg(m_pDevice->GetDevice(), m_pDevice->GetGraphicsQueue(), m_pScreenShotName.c_str());
		m_pScreenShotName.clear();
	}

	m_frame += 1;
}


//--------------------------------------------------------------------------------------
//
// OnResizeShadowMapWidth
//
//--------------------------------------------------------------------------------------
void Renderer::OnResizeShadowMapWidth(const UIState* pState)
{
	m_shadowMap.OnDestroy();

	m_shadowMap.InitDepthStencil(m_pDevice, "m_pShadowMap", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D16_UNORM, pState->shadowMapWidth, pState->shadowMapWidth, pState->numCascades, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
	m_shadowMap.CreateDSV(0, &m_ShadowMapDSV, 0, pState->numCascades);
	for (int i = 0; i < pState->numCascades; ++i)
	{
		m_shadowMap.CreateDSV(i + 1, &m_ShadowMapDSV, i);
	}
	m_shadowMap.CreateSRV(0, &m_ShadowMapSRV);


	// Set viewport and scissor rect for shadow map passes
	m_shadowViewport = { 0.0f , 0.0f, static_cast<float>(pState->shadowMapWidth), static_cast<float>(pState->shadowMapWidth), 0.0f, 1.0f };
	m_shadowRectScissor = { 0 , 0, static_cast<LONG>(pState->shadowMapWidth), static_cast<LONG>(pState->shadowMapWidth) };

	m_shadowTrace.BindShadowTexture(m_shadowMap);
}

