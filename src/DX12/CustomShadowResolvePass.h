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
#pragma once

using namespace CAULDRON_DX12;

class CustomShadowResolveFrame
{
public:
    uint32_t m_Width;
    uint32_t m_Height;
    CBV_SRV_UAV m_ShadowMapSRV;
    CBV_SRV_UAV m_DepthBufferSRV;
    CBV_SRV_UAV m_ShadowBufferUAV;
};

class CustomShadowResolvePass
{
    const int s_TileSize = 16;

public:
    struct per_frame
    {
        math::Matrix4   m_mInverseCameraCurrViewProj;
        math::Matrix4   m_mLightView;
        math::Vector4   m_vCascadeOffset[4];
        math::Vector4   m_vCascadeScale[4];

        UINT         m_nCascadeLevels; // Number of Cascades
        UINT         m_nTextureSizeX; // 1 is to visualize the cascades in different colors. 0 is to just draw the scene.
        UINT         m_nTextureSizeY; // 1 is to visualize the cascades in different colors. 0 is to just draw the scene.
        UINT         _pad;

        // For Map based selection scheme, this keeps the pixels inside of the the valid range.
        // When there is no boarder, these values are 0 and 1 respectivley.
        FLOAT       m_fMinBorderPadding;
        FLOAT       m_fMaxBorderPadding;
        FLOAT       m_fShadowBiasFromGUI;  // A shadow map offset to deal with self shadow artifacts.  
                                            //These artifacts are aggravated by PCF.
        FLOAT       m_fCascadeBlendArea; // Amount to overlap when blending between cascades.
        
        FLOAT       m_fLightDir[3];
        FLOAT       m_fSunSize;
    };

    void OnCreate(
        Device* pDevice,
        ResourceViewHeaps* pResourceViewHeaps,
        DynamicBufferRing* pDynamicBufferRing);
    void OnDestroy();

    CustomShadowResolvePass::per_frame* SetPerFrameConstants();

    void Draw(ID3D12GraphicsCommandList* pCommandList, GLTFTexturesAndBuffers* pGLTFTexturesAndBuffers, CustomShadowResolveFrame* pShadowResolveFrame, uint32_t shadowMapIndex);

protected:
    ResourceViewHeaps* m_pResourceViewHeaps;
    DynamicBufferRing* m_pDynamicBufferRing;

    ID3D12RootSignature* m_pRootSignature;
    ID3D12PipelineState* m_pPipelineState;

    D3D12_GPU_VIRTUAL_ADDRESS m_perFrameDesc;
};
