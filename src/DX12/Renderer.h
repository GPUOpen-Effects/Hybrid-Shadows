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

#include "stdafx.h"

#include "base/GBuffer.h"
#include "PostProc/MagnifierPS.h"

#include "Raytracer.h"
#include "ShadowRaytracer.h"

struct UIState;

// We are queuing (backBufferCount + 0.5) frames, so we need to triple buffer the resources that get modified each frame
static const int backBufferCount = 3;

using namespace CAULDRON_DX12;

//
// Renderer class is responsible for rendering resources management and recording command buffers.
class Renderer
{
public:
    void OnCreate(Device* pDevice, SwapChain *pSwapChain, float fontSize, const UIState* pState);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(SwapChain *pSwapChain, uint32_t Width, uint32_t Height, const UIState *pState);
    void OnDestroyWindowSizeDependentResources();

    void OnUpdateDisplayDependentResources(SwapChain *pSwapChain);

    int LoadScene(GLTFCommon *pGLTFCommon, int stage = 0);
    void UnloadScene();


    const std::vector<TimeStamp>& GetTimingValues() const { return m_TimeStamps; }
    std::string& GetScreenshotFileName() { return m_pScreenShotName; }

    void OnRender(const UIState* pState, const Camera& cam, SwapChain* pSwapChain);

    void OnResizeShadowMapWidth(const UIState* pState);

private:
    Device                         *m_pDevice;

    uint32_t                        m_frame;
    uint32_t                        m_Width;
    uint32_t                        m_Height;
    D3D12_VIEWPORT                  m_viewport;
    D3D12_RECT                      m_rectScissor;
    D3D12_VIEWPORT                  m_shadowViewport;
    D3D12_RECT                      m_shadowRectScissor;
    bool                            m_HasTAA = false;

    // Initialize helper classes
    ResourceViewHeaps               m_resourceViewHeaps;
    UploadHeap                      m_UploadHeap;
    DynamicBufferRing               m_ConstantBufferRing;
    StaticBufferPool                m_VidMemBufferPool;
    CommandListRing                 m_CommandListRing;
    GPUTimestamps                   m_GPUTimer;

    //gltf passes
    GltfPbrPass                    *m_gltfPBR;
    GltfBBoxPass                   *m_gltfBBox;
    GltfDepthPass                  *m_gltfDepth;
    GltfMotionVectorsPass          *m_gltfMotionVector;
    GLTFTexturesAndBuffers         *m_pGLTFTexturesAndBuffers;

    // effects
    Bloom                           m_bloom;
    SkyDome                         m_skyDome;
    DownSamplePS                    m_downSample;
    SkyDomeProc                     m_skyDomeProc;
    ToneMapping                     m_toneMappingPS;
    ToneMappingCS                   m_toneMappingCS;
    ColorConversionPS               m_colorConversionPS;
    TAA                             m_TAA;
    MagnifierPS                     m_magnifierPS;

    // GUI
    ImGUI                           m_ImGUI;

    // Temporary render targets
    GBuffer                         m_GBuffer;
    GBufferRenderPass               m_renderPassPrePass;
    GBufferRenderPass               m_renderPassJustDepthAndHdr;

    // shadow mask
    Texture                         m_ShadowMask;
    CBV_SRV_UAV                     m_ShadowMaskUAV;
    CBV_SRV_UAV                     m_ShadowMaskSRV;
    CustomShadowResolvePass         m_customShadowResolve;

    // shadowmaps

    // shadow map
    Texture                         m_shadowMap;
    DSV                             m_ShadowMapDSV;
    CBV_SRV_UAV                     m_ShadowMapSRV;

    CSMManager                      m_CSMManager;

    // widgets
    Wireframe                       m_wireframe;
    WireframeBox                    m_wireframeBox;

    std::vector<TimeStamp>          m_TimeStamps;

    // screen shot
    std::string                     m_pScreenShotName = "";
    SaveTexture                     m_saveTexture;
    AsyncPool                       m_asyncPool;

    Raytracing::ASBuffer m_scratchBuffer;
    Raytracing::ASFactory m_asFactory;

    Raytracing::ShadowTrace m_shadowTrace;

    Texture m_blueNoise;
};
