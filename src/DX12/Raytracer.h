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

#include "GLTF/GLTFTexturesAndBuffers.h"

namespace Raytracing
{
	class ASBuffer
	{
	public:
		ASBuffer(void);
		~ASBuffer(void);

		void OnCreate(Device* pDevice, uint32_t totalMemSize, bool isScratch, const char* name);
		void OnDestroy();

		D3D12_GPU_VIRTUAL_ADDRESS Suballoc(uint32_t byteSize);

		ID3D12Resource* GetResource(void) const;

		void Reset(void);
	private:
		uint32_t         m_memOffset;
		uint32_t         m_totalMemSize;

		ID3D12Resource* m_pBuffer;
	};

	class BLAS
	{
	public:
		BLAS(void);
		~BLAS(void);

		void Build(ID3D12GraphicsCommandList* pCmdList, ASBuffer& buffer);
		void PreBuild(CAULDRON_DX12::Device* pDevice);
		
		size_t GetStructureSize(void) const;
		D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(void) const;

		void AssignBuffer(D3D12_GPU_VIRTUAL_ADDRESS address);

		void AddGeometry(Geometry const& geo, DXGI_FORMAT vertexFormat, bool bIsOpaque);
		void SetMaskParams(uint32_t uvBufferOffset, uint32_t textureIndex);

		bool IsOpaque(void) const;
		uint32_t UVBufferOffset(void) const;
		uint32_t TextureIndex(void) const;
	private:
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_geometry;
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_inputs;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO m_info;
		
		D3D12_GPU_VIRTUAL_ADDRESS m_address;

		bool m_bIsBLASOpaque;
		uint32_t m_uvBufferOffset;
		uint32_t m_textureIndex;
	};

	class TLAS
	{
	public:
		TLAS(void);
		~TLAS(void);

		void Build(ID3D12GraphicsCommandList* pCmdList, ASBuffer& buffer, DynamicBufferRing& bufferRing);
		void PreBuild(CAULDRON_DX12::Device* pDevice);

		size_t GetStructureSize(void) const;
		D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(void) const;
		void AssignBuffer(D3D12_GPU_VIRTUAL_ADDRESS address);

		void AddInstance(BLAS const& blas, math::Matrix4 const& matrix);
	private:
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_instances;
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS m_inputs;
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO m_info;

		D3D12_GPU_VIRTUAL_ADDRESS m_address;
	};

	class ASFactory
	{
	public:
		ASFactory(void);
		~ASFactory(void);

		void OnCreate(CAULDRON_DX12::Device* pDevice);
		void OnDestroy();

		void BuildFromGltf(CAULDRON_DX12::Device* pDevice, GLTFTexturesAndBuffers* pGLTFTexturesAndBuffers, ResourceViewHeaps* pResourceViewHeaps, UploadHeap* pUpload);

		TLAS BuildTLASFromGLTF(CAULDRON_DX12::Device* pDevice, GLTFTexturesAndBuffers* pGLTFTexturesAndBuffers, bool bGatherOpaque, bool bGatherNonOpaque);
		void SyncTLASBuilds(ID3D12GraphicsCommandList* pCmdList);


		void ClearBuiltStructures(void);
		void ResetTLAS(void);

		CBV_SRV_UAV& GetMaskTextureTable(void);
		Texture* GetUVBuffer(void);
		std::vector<BLAS>& GetBLASVector(void);

	private:
		struct Mesh
		{
			std::vector<size_t> structs;
		};


		std::vector<ASBuffer*> m_buffers;
		std::vector<Texture*> m_alphaTextures;
		std::vector<BLAS> m_structures;
		std::vector<Mesh> m_meshes;

		ASBuffer m_tlasBuffer;

		CBV_SRV_UAV m_maskTextureTable;
		Texture m_blasUVBuffer;

		
	};
}