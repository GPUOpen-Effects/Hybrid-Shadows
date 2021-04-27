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

#include "CSMManager.h"

void CSMManager::OnCreate(int numCascades)
{
    m_matShadowProj.clear();
    m_matShadowProj.resize(numCascades);

    m_fCascadePartitionsFrustum.clear();
    m_fCascadePartitionsFrustum.resize(numCascades);

    m_vSceneAABBPointsLightSpaceCenter.clear();
    m_vSceneAABBPointsLightSpaceCenter.resize(numCascades);

    m_vSceneAABBPointsLightSpaceRadius.clear();
    m_vSceneAABBPointsLightSpaceRadius.resize(numCascades);
}

//--------------------------------------------------------------------------------------
// This function takes the camera's projection matrix and returns the 8
// points that make up a view frustum.
// The frustum is scaled to fit within the Begin and End interval paramaters.
//--------------------------------------------------------------------------------------
void CSMManager::CreateFrustumPointsFromCascadeInterval(float camNear, float fCascadeIntervalBegin,
    FLOAT fCascadeIntervalEnd,
    math::Matrix4 projection,
    math::Vector4* pvCornerPointsWorld)
{
    math::Matrix4 inverseProjection = math::inverse(projection);

    math::Vector4 topLeft = { -1.0f, 1.0f, 0.0f, 1.0f };
    math::Vector4 topRight = { 1.0f, 1.0f, 0.0f, 1.0f };
    math::Vector4 bottomLeft = { -1.0f, -1.0f, 0.0f, 1.0f };
    math::Vector4 bottomRight = { 1.0f, -1.0f, 0.0f, 1.0f };

    math::Vector4 camNearTopLeftView = inverseProjection * topLeft;
    camNearTopLeftView /= camNearTopLeftView.getW();

    math::Vector4 camNearTopRightView = inverseProjection * topRight;
    camNearTopRightView /= camNearTopRightView.getW();

    math::Vector4 camNearBottomLeftView = inverseProjection * bottomLeft;
    camNearBottomLeftView /= camNearBottomLeftView.getW();

    math::Vector4 camNearBottomRightView = inverseProjection * bottomRight;
    camNearBottomRightView /= camNearBottomRightView.getW();

    float beginIntervalScale = fCascadeIntervalBegin / camNear;
    float endIntervalScale = fCascadeIntervalEnd / camNear;

    pvCornerPointsWorld[0] = camNearTopLeftView * beginIntervalScale;
    pvCornerPointsWorld[0].setW(1.0f);

    pvCornerPointsWorld[1] = camNearTopRightView * beginIntervalScale;
    pvCornerPointsWorld[1].setW(1.0f);

    pvCornerPointsWorld[2] = camNearBottomLeftView * beginIntervalScale;
    pvCornerPointsWorld[2].setW(1.0f);

    pvCornerPointsWorld[3] = camNearBottomRightView * beginIntervalScale;
    pvCornerPointsWorld[3].setW(1.0f);

    pvCornerPointsWorld[4] = camNearTopLeftView * endIntervalScale;
    pvCornerPointsWorld[4].setW(1.0f);

    pvCornerPointsWorld[5] = camNearTopRightView * endIntervalScale;
    pvCornerPointsWorld[5].setW(1.0f);

    pvCornerPointsWorld[6] = camNearBottomLeftView * endIntervalScale;
    pvCornerPointsWorld[6].setW(1.0f);

    pvCornerPointsWorld[7] = camNearBottomRightView * endIntervalScale;
    pvCornerPointsWorld[7].setW(1.0f);
}

void CSMManager::SetupCascades(math::Matrix4 matCameraProjection, math::Matrix4 matViewCameraView,
    math::Matrix4 matLightCameraView, float camNear, GLTFCommon* pC, int numCascades, const float* pCascadeSplitPoints,
    int cascadeType, float width, bool bMoveLightTexelSize)
{
    math::Matrix4 matInverseViewCamera = math::affineInverse(matViewCameraView);

    // Find scene min/max
    math::Vector4 m_vSceneAABBMin = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
    math::Vector4 m_vSceneAABBMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };

    math::Vector4 vMeshCorner;

    math::Vector4 boxbounds[8];
    boxbounds[0] = math::Vector4(-1, -1, 1, 0);
    boxbounds[1] = math::Vector4(1, -1, 1, 0);
    boxbounds[2] = math::Vector4(1, 1, 1, 0);
    boxbounds[3] = math::Vector4(-1, 1, 1, 0);
    boxbounds[4] = math::Vector4(-1, -1, -1, 0);
    boxbounds[5] = math::Vector4(1, -1, -1, 0);
    boxbounds[6] = math::Vector4(1, 1, -1, 0);
    boxbounds[7] = math::Vector4(-1, 1, -1, 0);

    for (uint32_t i = 0; i < pC->m_nodes.size(); i++)
    {
        tfNode* pNode = &pC->m_nodes[i];
        if (pNode->meshIndex < 0)
            continue;

        math::Matrix4 mWorldTransformMatrix = pC->m_worldSpaceMats[i].GetCurrent();
        tfMesh* pMesh = &pC->m_meshes[pNode->meshIndex];
        for (uint32_t p = 0; p < pMesh->m_pPrimitives.size(); p++)
        {
            for (int j = 0; j < 8; ++j)
            {
                vMeshCorner = mWorldTransformMatrix * (pMesh->m_pPrimitives[p].m_center + math::mulPerElem(boxbounds[j], pMesh->m_pPrimitives[p].m_radius));
                m_vSceneAABBMin = math::SSE::minPerElem(vMeshCorner, m_vSceneAABBMin);
                m_vSceneAABBMax = math::SSE::maxPerElem(vMeshCorner, m_vSceneAABBMax);
            }
        }
    }

    math::Vector4 m_vSceneAABBCenter = (m_vSceneAABBMin + m_vSceneAABBMax) * 0.5f;
    math::Vector4 m_vSceneAABBRadius = (m_vSceneAABBMax - m_vSceneAABBMin) * 0.5f;

    // Lets get scene bounding box in light space
    math::Vector4 vSceneAABBPointsLightSpace[8];

    for (int i = 0; i < 8; ++i)
    {
        vSceneAABBPointsLightSpace[i] = matLightCameraView * (m_vSceneAABBCenter + math::mulPerElem(boxbounds[i], m_vSceneAABBRadius));
    }

    //These are the unconfigured near and far plane values.  They are purposly awful to show 
    // how important calculating accurate near and far planes is.
    FLOAT fNearPlane = 0.0f;
    FLOAT fFarPlane = 10000.0f;

    math::Vector4 vLightSpaceSceneAABBminValue = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };  // world space scene aabb 
    math::Vector4 vLightSpaceSceneAABBmaxValue = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };
    // We calculate the min and max vectors of the scene in light space. The min and max "Z" values of the  
    // light space AABB can be used for the near and far plane. This is easier than intersecting the scene with the AABB
    // and in some cases provides similar results.
    for (int index = 0; index < 8; ++index)
    {
        vLightSpaceSceneAABBminValue = math::SSE::minPerElem(vSceneAABBPointsLightSpace[index], vLightSpaceSceneAABBminValue);
        vLightSpaceSceneAABBmaxValue = math::SSE::maxPerElem(vSceneAABBPointsLightSpace[index], vLightSpaceSceneAABBmaxValue);
    }

    // The min and max z values are the near and far planes.
    fNearPlane = vLightSpaceSceneAABBminValue.getZ();
    fFarPlane = vLightSpaceSceneAABBmaxValue.getZ();

    FLOAT fFrustumIntervalBegin, fFrustumIntervalEnd;
    math::Vector4 vLightCameraOrthographicMin;  // light space frustrum aabb 
    math::Vector4 vLightCameraOrthographicMax;

    math::Point3 sceneMin = math::Point3(m_vSceneAABBMin.getXYZ());
    math::Point3 sceneMax = math::Point3(m_vSceneAABBMax.getXYZ());
    FLOAT fSceneNearFarRange = math::SSE::dist(sceneMin, sceneMax);

    math::Vector4 vWorldUnitsPerTexel = { 0.0f, 0.0f, 0.0f, 0.0f };

    // We loop over the cascades to calculate the orthographic projection for each cascade.
    for (INT iCascadeIndex = 0; iCascadeIndex < numCascades; ++iCascadeIndex)
    {
        // Calculate the interval of the View Frustum that this cascade covers. We measure the interval 
        // the cascade covers as a Min and Max distance along the Z Axis.
        if (cascadeType == FIT_TO_CASCADES)
        {
            // Because we want to fit the orthogrpahic projection tightly around the Cascade, we set the Mimiumum cascade 
            // value to the previous Frustum end Interval
            if (iCascadeIndex == 0) fFrustumIntervalBegin = 0.0f;
            else fFrustumIntervalBegin = pCascadeSplitPoints[iCascadeIndex - 1];
        }
        else
        {
            // In the FIT_TO_SCENE technique the Cascades overlap eachother.  In other words, interval 1 is coverd by
            // cascades 1 to 8, interval 2 is covered by cascades 2 to 8 and so forth.
            fFrustumIntervalBegin = 0.0f;
        }

        // Scale the intervals between 0 and 1. They are now percentages that we can scale with.
        fFrustumIntervalEnd = pCascadeSplitPoints[iCascadeIndex];
        const float maxCascadeSplitPoint = 100;
        fFrustumIntervalBegin /= maxCascadeSplitPoint;
        fFrustumIntervalEnd /= maxCascadeSplitPoint;

        if (iCascadeIndex == numCascades - 1)
        {
            fFrustumIntervalEnd = 1; // last cascade goes to the edge of the scene
        }

        fFrustumIntervalBegin = fFrustumIntervalBegin * fSceneNearFarRange;
        fFrustumIntervalEnd = fFrustumIntervalEnd * fSceneNearFarRange;

        // Lets get bounding box of current frustum
        math::Vector4 vFrustumPoints[8];
        CreateFrustumPointsFromCascadeInterval(camNear, fFrustumIntervalBegin, fFrustumIntervalEnd,
            matCameraProjection, vFrustumPoints);

        // center + radius
        math::Vector4 vFrustumPointsMin = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
        math::Vector4 vFrustumPointsMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };

        for (int i = 0; i < 8; ++i)
        {
            // Find the closest point.
            vFrustumPointsMin = math::SSE::minPerElem(vFrustumPoints[i], vFrustumPointsMin);
            vFrustumPointsMax = math::SSE::maxPerElem(vFrustumPoints[i], vFrustumPointsMax);
        }

        m_vFrustumPointsAABBCenter = (vFrustumPointsMin + vFrustumPointsMax) * 0.5f;
        m_vFrustumPointsAABBRadius = (vFrustumPointsMax - vFrustumPointsMin) * 0.5f;

        vLightCameraOrthographicMin = { FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX };
        vLightCameraOrthographicMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX };

        // Lets get bounding box of frustum after translating it into light view space
        math::Vector4 vTempTranslatedCornerPoint;
        for (int icpIndex = 0; icpIndex < 8; ++icpIndex)
        {
            // Transform the frustum from camera view space to world space.
            vFrustumPoints[icpIndex] = matInverseViewCamera * vFrustumPoints[icpIndex];
            // Transform the point from world space to Light Camera Space.
            vTempTranslatedCornerPoint = matLightCameraView * vFrustumPoints[icpIndex];

            // Find the closest point.
            vLightCameraOrthographicMin = math::SSE::minPerElem(vTempTranslatedCornerPoint, vLightCameraOrthographicMin);
            vLightCameraOrthographicMax = math::SSE::maxPerElem(vTempTranslatedCornerPoint, vLightCameraOrthographicMax);
        }

        m_vLightCameraAABBCenter = (vLightCameraOrthographicMin + vLightCameraOrthographicMax) * 0.5f;
        m_vLightCameraAABBRadius = (vLightCameraOrthographicMax - vLightCameraOrthographicMin) * 0.5f;

        const math::Vector4 g_vHalfVector = { 0.5f, 0.5f, 0.5f, 0.5f };
        const math::Vector4 g_vMultiplySetzwToZero = { 1.0f, 1.0f, 0.0f, 0.0f };

        // This code removes the shimmering effect along the edges of shadows due to
        // the light changing to fit the camera.
        if (cascadeType == FIT_TO_SCENE)
        {
            // Fit the ortho projection to the cascades far plane and a near plane of zero. 
            // Pad the projection to be the size of the diagonal of the Frustum partition. 
            // 
            // To do this, we pad the ortho transform so that it is always big enough to cover 
            // the entire camera view frustum.
            math::Vector4 vDiagonal = vFrustumPoints[0] - vFrustumPoints[6];

            // The bound is the length of the diagonal of the frustum interval.
            FLOAT fCascadeBound = math::SSE::length(vDiagonal);
            vDiagonal = { fCascadeBound, fCascadeBound, fCascadeBound, fCascadeBound };

            // The offset calculated will pad the ortho projection so that it is always the same size 
            // and big enough to cover the entire cascade interval.

            math::Vector4 vBoarderOffset = math::mulPerElem((vDiagonal -
                (vLightCameraOrthographicMax - vLightCameraOrthographicMin)), g_vHalfVector);

            // Set the Z and W components to zero.
            vBoarderOffset = math::mulPerElem(vBoarderOffset, g_vMultiplySetzwToZero);

            // Add the offsets to the projection.
            vLightCameraOrthographicMax += vBoarderOffset;
            vLightCameraOrthographicMin -= vBoarderOffset;

            // The world units per texel are used to snap the shadow the orthographic projection
            // to texel sized increments.  This keeps the edges of the shadows from shimmering.
            FLOAT fWorldUnitsPerTexel = fCascadeBound / width;
            vWorldUnitsPerTexel = math::Vector4(fWorldUnitsPerTexel, fWorldUnitsPerTexel, 0.0f, 0.0f);


        }
        else if (cascadeType == FIT_TO_CASCADES)
        {
            float fNormalizeByBufferSize = (1.0f / width);
            math::Vector4 vNormalizeByBufferSize = math::Vector4(fNormalizeByBufferSize, fNormalizeByBufferSize, 0.0f, 0.0f);

            // We calculate the offsets as a percentage of the bound.
            math::Vector4 vBoarderOffset = vLightCameraOrthographicMax - vLightCameraOrthographicMin;
            vBoarderOffset = math::mulPerElem(vBoarderOffset, g_vHalfVector);
            vLightCameraOrthographicMax += vBoarderOffset;
            vLightCameraOrthographicMin -= vBoarderOffset;

            // The world units per texel are used to snap  the orthographic projection
            // to texel sized increments.  
            // Because we're fitting tighly to the cascades, the shimmering shadow edges will still be present when the 
            // camera rotates.  However, when zooming in or strafing the shadow edge will not shimmer.
            vWorldUnitsPerTexel = vLightCameraOrthographicMax - vLightCameraOrthographicMin;
            vWorldUnitsPerTexel = math::mulPerElem(vWorldUnitsPerTexel, vNormalizeByBufferSize);

        }

        if (bMoveLightTexelSize)
        {
            // We snape the camera to 1 pixel increments so that moving the camera does not cause the shadows to jitter.
            // This is a matter of integer dividing by the world space size of a texel
            vLightCameraOrthographicMin = math::divPerElem(vLightCameraOrthographicMin, vWorldUnitsPerTexel);
            vLightCameraOrthographicMin = math::Vector4(floorf(vLightCameraOrthographicMin.getX()), floorf(vLightCameraOrthographicMin.getY()), floorf(vLightCameraOrthographicMin.getZ()), floorf(vLightCameraOrthographicMin.getW()));
            vLightCameraOrthographicMin = math::mulPerElem(vLightCameraOrthographicMin, vWorldUnitsPerTexel);

            vLightCameraOrthographicMax = math::divPerElem(vLightCameraOrthographicMax, vWorldUnitsPerTexel);
            vLightCameraOrthographicMax = math::Vector4(floorf(vLightCameraOrthographicMax.getX()), floorf(vLightCameraOrthographicMax.getY()), floorf(vLightCameraOrthographicMax.getZ()), floorf(vLightCameraOrthographicMax.getW()));
            vLightCameraOrthographicMax = math::mulPerElem(vLightCameraOrthographicMax, vWorldUnitsPerTexel);
        }

        m_vSceneAABBPointsLightSpaceCenter[iCascadeIndex] = (vLightSpaceSceneAABBminValue + vLightSpaceSceneAABBmaxValue) * 0.5f;
        m_vSceneAABBPointsLightSpaceRadius[iCascadeIndex] = (vLightSpaceSceneAABBmaxValue - vLightSpaceSceneAABBminValue) * 0.5f;

        // Create the orthographic projection for this cascade.
        m_matShadowProj[iCascadeIndex] = math::Matrix4::orthographic(vLightCameraOrthographicMin.getX(), vLightCameraOrthographicMax.getX(),
            vLightCameraOrthographicMin.getY(), vLightCameraOrthographicMax.getY(),
            fNearPlane, fFarPlane);

        m_matShadowProj[iCascadeIndex].setCol2(m_matShadowProj[iCascadeIndex].getCol2() / 2.0f);

        math::Vector4 vec = m_matShadowProj[iCascadeIndex].getCol3();
        vec.setZ(-(fFarPlane / (fNearPlane - fFarPlane)));
        m_matShadowProj[iCascadeIndex].setCol3(vec);

        m_fCascadePartitionsFrustum[iCascadeIndex] = fFrustumIntervalEnd;
    }
}