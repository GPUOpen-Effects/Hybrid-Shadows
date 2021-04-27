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

// The MIT License(MIT)
// 
// Copyright(c) 2004 - 2021 Microsoft Corp
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// softwareand associated documentation files(the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and /or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions :
// 
// The above copyright noticeand this permission notice shall be included in all copies
// or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


// Code reference: https://github.com/walbourn/directx-sdk-samples/tree/master/CascadedShadowMaps11

#pragma once

class CSMManager
{
public:

    void OnCreate(int numCascades);
    void CreateFrustumPointsFromCascadeInterval(float camNear, float fCascadeIntervalBegin,
        FLOAT fCascadeIntervalEnd,
        math::Matrix4 projection,
        math::Vector4* pvCornerPointsWorld);
    void SetupCascades(math::Matrix4 matCameraProjection, math::Matrix4 matViewCameraView,
        math::Matrix4 matLightCameraView, float camNear, GLTFCommon* pC, int numCascades, float const* pCascadeSplitPoints,
        int cascadeType, float width, bool bMoveLightTexelSize);
    const std::vector<math::Matrix4> GetShadowProj() { return m_matShadowProj; }
    const std::vector<float> GetCascadePartitionsFrustum() { return m_fCascadePartitionsFrustum; }

    const math::Vector4 GetLightCameraAABBCenter() { return m_vLightCameraAABBCenter; }
    const math::Vector4 GetLightCameraAABBRadius() { return m_vLightCameraAABBRadius; }

    const math::Vector4 GetFrustumPointsAABBCenter() { return m_vFrustumPointsAABBCenter; }
    const math::Vector4 GetFrustumPointsAABBRadius() { return m_vFrustumPointsAABBRadius; }

    std::vector<math::Vector4> GetSceneAABBPointsLightSpaceCenter() { return m_vSceneAABBPointsLightSpaceCenter; }
    std::vector<math::Vector4> GetSceneAABBPointsLightSpaceRadius() { return m_vSceneAABBPointsLightSpaceRadius; }

private:

    math::Vector4 m_vSceneAABBMin;
    math::Vector4 m_vSceneAABBMax;

    std::vector<math::Matrix4> m_matShadowProj;
    std::vector<float> m_fCascadePartitionsFrustum;

    math::Vector4 m_vLightCameraAABBCenter;
    math::Vector4 m_vLightCameraAABBRadius;

    math::Vector4 m_vFrustumPointsAABBCenter;
    math::Vector4 m_vFrustumPointsAABBRadius;

    std::vector<math::Vector4> m_vSceneAABBPointsLightSpaceCenter;
    std::vector<math::Vector4> m_vSceneAABBPointsLightSpaceRadius;

    //--------------------------------------------------------------------------------------
    // Used to compute an intersection of the orthographic projection and the Scene AABB
    //--------------------------------------------------------------------------------------
    struct Triangle
    {
        math::Vector4 pt[3];
        bool culled;
    };

    enum FIT_PROJECTION_TO_CASCADES
    {
        FIT_TO_CASCADES,
        FIT_TO_SCENE
    };
};
