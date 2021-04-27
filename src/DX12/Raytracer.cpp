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

#include "Raytracer.h"
#include "GLTF/GltfHelpers.h"

namespace
{
	uint32_t GetIndex(tfAccessor const& indexBuffer, uint32_t indexIndex)
	{
		void const* pIndex = indexBuffer.Get(indexIndex);

		uint32_t index = ~0;
		switch (indexBuffer.m_stride)
		{
		case 1:
			index = *reinterpret_cast<uint8_t const*>(pIndex);
			break;
		case 2:
			index = *reinterpret_cast<uint16_t const*>(pIndex);
			break;
		case 4:
			index = *reinterpret_cast<uint32_t const*>(pIndex);
			break;
		default:
			break;
		}

		return index;
	}

	math::Point2 GetUV(tfAccessor const& uvBuffer, uint32_t indexIndex)
	{
		float const* pUV = reinterpret_cast<float const*>(uvBuffer.Get(indexIndex));

		assert(uvBuffer.m_stride == 8);

		math::Point2 uv = math::Point2(pUV[0], pUV[1]);

		return uv;
	}

	///////////////////////////////////////////////////////////////////////////
	//
	// Copyright (c) 2002, Industrial Light & Magic, a division of Lucas
	// Digital Ltd. LLC
	// 
	// All rights reserved.
	// 
	// Redistribution and use in source and binary forms, with or without
	// modification, are permitted provided that the following conditions are
	// met:
	// *       Redistributions of source code must retain the above copyright
	// notice, this list of conditions and the following disclaimer.
	// *       Redistributions in binary form must reproduce the above
	// copyright notice, this list of conditions and the following disclaimer
	// in the documentation and/or other materials provided with the
	// distribution.
	// *       Neither the name of Industrial Light & Magic nor the names of
	// its contributors may be used to endorse or promote products derived
	// from this software without specific prior written permission. 
	// 
	// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
	// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
	// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	//
	///////////////////////////////////////////////////////////////////////////
	uint16_t ConvertToHalf(uint32_t i)
	{
		//
		// Our floating point number, f, is represented by the bit
		// pattern in integer i.  Disassemble that bit pattern into
		// the sign, s, the exponent, e, and the significand, m.
		// Shift s into the position where it will go in in the
		// resulting half number.
		// Adjust e, accounting for the different exponent bias
		// of float and half (127 versus 15).
		//

		int s = (i >> 16) & 0x00008000;
		int e = ((i >> 23) & 0x000000ff) - (127 - 15);
		int m = i & 0x007fffff;

		//
		// Now reassemble s, e and m into a half:
		//

		if (e <= 0)
		{
			if (e < -10)
			{
				//
				// E is less than -10.  The absolute value of f is
				// less than HALF_MIN (f may be a small normalized
				// float, a denormalized float or a zero).
				//
				// We convert f to a half zero with the same sign as f.
				//

				return s;
			}

			//
			// E is between -10 and 0.  F is a normalized float
			// whose magnitude is less than HALF_NRM_MIN.
			//
			// We convert f to a denormalized half.
			//

			//
			// Add an explicit leading 1 to the significand.
			// 

			m = m | 0x00800000;

			//
			// Round to m to the nearest (10+e)-bit value (with e between
			// -10 and 0); in case of a tie, round to the nearest even value.
			//
			// Rounding may cause the significand to overflow and make
			// our number normalized.  Because of the way a half's bits
			// are laid out, we don't have to treat this case separately;
			// the code below will handle it correctly.
			// 

			int t = 14 - e;
			int a = (1 << (t - 1)) - 1;
			int b = (m >> t) & 1;

			m = (m + a + b) >> t;

			//
			// Assemble the half from s, e (zero) and m.
			//

			return s | m;
		}
		else if (e == 0xff - (127 - 15))
		{
			if (m == 0)
			{
				//
				// F is an infinity; convert f to a half
				// infinity with the same sign as f.
				//

				return s | 0x7c00;
			}
			else
			{
				//
				// F is a NAN; we produce a half NAN that preserves
				// the sign bit and the 10 leftmost bits of the
				// significand of f, with one exception: If the 10
				// leftmost bits are all zero, the NAN would turn 
				// into an infinity, so we have to set at least one
				// bit in the significand.
				//

				m >>= 13;
				return s | 0x7c00 | m | (m == 0);
			}
		}
		else
		{
			//
			// E is greater than zero.  F is a normalized float.
			// We try to convert f to a normalized half.
			//

			//
			// Round to m to the nearest 10-bit value.  In case of
			// a tie, round to the nearest even value.
			//

			m = m + 0x00000fff + ((m >> 13) & 1);

			if (m & 0x00800000)
			{
				m = 0;		// overflow in significand,
				e += 1;		// adjust exponent
			}

			//
			// Handle exponent overflow
			//

			if (e > 30)
			{
				return s | 0x7c00;	// if this returns, the half becomes an
			}   			// infinity with the same sign as f.

			//
			// Assemble the half from s, e and m.
			//

			return s | (e << 10) | (m >> 13);
		}
	}
	uint16_t ToF16(float const& f)
	{
		uint32_t x = *((uint32_t*)&f);
		uint16_t h = ConvertToHalf(x);

		return h;
	}

	struct UV
	{
		math::Point2 uv0;
		uint16_t uv01[2];
		uint16_t uv02[2];
	};
}

namespace Raytracing
{
	BLAS::BLAS(void)
		: m_geometry()
		, m_inputs{}
		, m_info{}
		, m_address()
		, m_bIsBLASOpaque(true)
		, m_uvBufferOffset(~0u)
		, m_textureIndex(~0u)
	{
	}

	BLAS::~BLAS(void)
	{
	}

	void BLAS::Build(ID3D12GraphicsCommandList* pCmdList, ASBuffer& buffer)
	{
		UserMarker marker(pCmdList, "BLAS Build");

		D3D12_GPU_VIRTUAL_ADDRESS address = buffer.Suballoc((uint32_t)m_info.ScratchDataSizeInBytes);
		if (address == 0)
		{
			// buffer is full so lets do a barrier and reset the buffer
			buffer.Reset();

			pCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));
			address = buffer.Suballoc((uint32_t)m_info.ScratchDataSizeInBytes);
			assert(address != 0);
		}

		ID3D12GraphicsCommandList4* pCmdList4 = nullptr;
		pCmdList->QueryInterface(&pCmdList4);

		// make sure the ptr is correct.
		m_inputs.pGeometryDescs = m_geometry.data();

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
		
		desc.DestAccelerationStructureData = m_address;
		desc.Inputs = m_inputs;
		desc.ScratchAccelerationStructureData = address;

		assert(desc.DestAccelerationStructureData != 0);
		assert(desc.ScratchAccelerationStructureData != 0);

		pCmdList4->BuildRaytracingAccelerationStructure(&desc,
			0, nullptr);

		pCmdList4->Release();
	}

	void BLAS::PreBuild(CAULDRON_DX12::Device* pDevice)
	{
		auto device = pDevice->GetDevice();
		ID3D12Device5* pDevice5 = nullptr;
		device->QueryInterface(&pDevice5);

		m_inputs = {};
		m_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		m_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		m_inputs.NumDescs = static_cast<UINT>(m_geometry.size());
		m_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		m_inputs.pGeometryDescs = m_geometry.data();

		m_info = {};
		pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&m_inputs, &m_info);
		pDevice5->Release();
	}

	size_t BLAS::GetStructureSize(void) const
	{
		return m_info.ResultDataMaxSizeInBytes;
	}

	D3D12_GPU_VIRTUAL_ADDRESS BLAS::GetGpuAddress(void) const
	{
		return m_address;
	}

	void BLAS::AssignBuffer(D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		m_address = address;
	}

	void BLAS::AddGeometry(Geometry const& geometry, DXGI_FORMAT vertexFormat, bool bIsOpaque)
	{
		m_bIsBLASOpaque = m_bIsBLASOpaque && bIsOpaque;

		D3D12_RAYTRACING_GEOMETRY_DESC geo = {};
		geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geo.Flags = (bIsOpaque) ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		geo.Triangles.Transform3x4 = 0; // no local transform
		geo.Triangles.IndexFormat = geometry.m_indexType;
		geo.Triangles.VertexFormat = vertexFormat;
		geo.Triangles.IndexCount = geometry.m_NumIndices;
		geo.Triangles.VertexCount = geometry.m_VBV[0].SizeInBytes / geometry.m_VBV[0].StrideInBytes;
		geo.Triangles.IndexBuffer = geometry.m_IBV.BufferLocation;
		geo.Triangles.VertexBuffer.StartAddress = geometry.m_VBV[0].BufferLocation;
		geo.Triangles.VertexBuffer.StrideInBytes = geometry.m_VBV[0].StrideInBytes;
		m_geometry.push_back(geo);
	}

	void BLAS::SetMaskParams(uint32_t uvBufferOffset, uint32_t textureIndex)
	{
		m_uvBufferOffset = uvBufferOffset;
		m_textureIndex = textureIndex;
	}

	bool BLAS::IsOpaque(void) const
	{
		return m_bIsBLASOpaque;
	}

	uint32_t BLAS::UVBufferOffset(void) const
	{
		return m_uvBufferOffset;
	}

	uint32_t BLAS::TextureIndex(void) const
	{
		return m_textureIndex;
	}

	TLAS::TLAS(void)
		: m_instances()
		, m_inputs{}
		, m_info{}
		, m_address()
	{
	}

	TLAS::~TLAS(void)
	{
	}

	void TLAS::Build(ID3D12GraphicsCommandList* pCmdList, ASBuffer& buffer, DynamicBufferRing& bufferRing)
	{
		UserMarker marker(pCmdList, "TLAS Build");

		D3D12_GPU_VIRTUAL_ADDRESS address = buffer.Suballoc((uint32_t)m_info.ScratchDataSizeInBytes);
		if (address == 0)
		{
			// buffer is full so lets do a barrier and reset the buffer
			buffer.Reset();

			pCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(buffer.GetResource()));
			address = buffer.Suballoc((uint32_t)m_info.ScratchDataSizeInBytes);
			assert(address != 0);
		}

		ID3D12GraphicsCommandList4* pCmdList4 = nullptr;
		pCmdList->QueryInterface(&pCmdList4);

		D3D12_GPU_VIRTUAL_ADDRESS instanceAddress = bufferRing.AllocConstantBuffer((uint32_t)sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * (uint32_t)m_instances.size(), m_instances.data());
		assert(instanceAddress != 0);
		m_inputs.InstanceDescs = instanceAddress;

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};

		desc.DestAccelerationStructureData = m_address;
		desc.Inputs = m_inputs;
		desc.ScratchAccelerationStructureData = address;

		assert(desc.DestAccelerationStructureData != 0);
		assert(desc.ScratchAccelerationStructureData != 0);

		pCmdList4->BuildRaytracingAccelerationStructure(&desc,
			0, nullptr);

		pCmdList4->Release();
	}

	void TLAS::PreBuild(CAULDRON_DX12::Device* pDevice)
	{
		auto device = pDevice->GetDevice();
		ID3D12Device5* pDevice5 = nullptr;
		device->QueryInterface(&pDevice5);

		m_inputs = {};
		m_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		m_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		m_inputs.NumDescs = static_cast<UINT>(m_instances.size());
		m_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		m_inputs.InstanceDescs = 0; // should not need this for prebuild info since the cpu is able to touch it

		m_info = {};
		pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&m_inputs, &m_info);
		pDevice5->Release();
	}

	size_t TLAS::GetStructureSize(void) const
	{
		return m_info.ResultDataMaxSizeInBytes;
	}

	D3D12_GPU_VIRTUAL_ADDRESS TLAS::GetGpuAddress(void) const
	{
		return m_address;
	}

	void TLAS::AssignBuffer(D3D12_GPU_VIRTUAL_ADDRESS address)
	{
		m_address = address;
	}

	void TLAS::AddInstance(BLAS const& blas, math::Matrix4 const& matrix)
	{
		D3D12_RAYTRACING_INSTANCE_DESC desc = {};
		memcpy(desc.Transform, math::toFloatPtr(math::transpose(matrix)), sizeof(desc.Transform));
		desc.InstanceID = blas.UVBufferOffset(); // using the id as the offset into the UV buffer
		desc.InstanceMask = 0xFF;
		desc.InstanceContributionToHitGroupIndex = blas.TextureIndex(); // using this for the texture index. This will only work with inline raytracing
		desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		desc.AccelerationStructure = blas.GetGpuAddress();

		m_instances.emplace_back(std::move(desc));
	}

	ASFactory::ASFactory(void)
		: m_buffers()
		, m_structures()
		, m_meshes()
		, m_tlasBuffer()
	{
	}

	ASFactory::~ASFactory(void)
	{
	}

	void ASFactory::OnCreate(CAULDRON_DX12::Device* pDevice)
	{
		m_tlasBuffer.OnCreate(pDevice, 256 * 1024 * 1024, false, "TLAS");
	}

	void ASFactory::OnDestroy()
	{
		m_tlasBuffer.OnDestroy();
		ClearBuiltStructures();
	}

	void ASFactory::BuildFromGltf(CAULDRON_DX12::Device* pDevice, GLTFTexturesAndBuffers* pGLTFTexturesAndBuffers, ResourceViewHeaps* pResourceViewHeaps, UploadHeap* pUpload)
	{
		const json& j3 = pGLTFTexturesAndBuffers->m_pGLTFCommon->j3;

		std::vector<UV> postProcessedUVs;

		//
		if (j3.find("meshes") != j3.end())
		{
			const json& materials = j3["materials"];
			const json& meshes = j3["meshes"];
			m_meshes.resize(meshes.size());
			for (uint32_t i = 0; i < meshes.size(); i++)
			{
				const json& primitives = meshes[i]["primitives"];
				m_meshes[i].structs.resize(primitives.size());
				for (uint32_t p = 0; p < primitives.size(); p++)
				{
					const json& primitive = primitives[p];

					int indexBufferId = primitive.value("indices", -1);
					const json& attributes = primitive.at("attributes");
					int const attr = attributes.find("POSITION").value();
					std::vector<int> requiredAttributes;
					requiredAttributes.push_back(attr);

					Geometry geometry = { };
					pGLTFTexturesAndBuffers->CreateGeometry(indexBufferId, requiredAttributes, &geometry);

					uint32_t uvOffset = ~0;
					uint32_t textureIndex = ~0;
					bool bIsOpaque = true;
					auto mat = primitive.find("material");
					if (mat != primitive.end())
					{
						auto material = materials[(size_t)mat.value()];
						bIsOpaque = GetElementString(material, "alphaMode", "OPAQUE") != "MASK";
						// todo: filter out blends
						// todo: pass alpha cut out value

						if (GetElementString(material, "alphaMode", "OPAQUE") == "BLEND")
							continue;

						if (bIsOpaque == false)
						{
							int const texAttr = attributes.find("TEXCOORD_0").value();

							tfAccessor vertexBufferAcc;
							pGLTFTexturesAndBuffers->m_pGLTFCommon->GetBufferDetails(texAttr, &vertexBufferAcc);

							tfAccessor indexBufferAcc;
							pGLTFTexturesAndBuffers->m_pGLTFCommon->GetBufferDetails(indexBufferId, &indexBufferAcc);

							uvOffset = (uint32_t)postProcessedUVs.size();
							for (uint32_t prim = 0; prim < (uint32_t)indexBufferAcc.m_count / 3; ++prim)
							{
								uint32_t i0 = GetIndex(indexBufferAcc, prim * 3 + 0);
								uint32_t i1 = GetIndex(indexBufferAcc, prim * 3 + 1);
								uint32_t i2 = GetIndex(indexBufferAcc, prim * 3 + 2);

								math::Point2 uv0 = GetUV(vertexBufferAcc, i0);
								math::Point2 uv1 = GetUV(vertexBufferAcc, i1);
								math::Point2 uv2 = GetUV(vertexBufferAcc, i2);

								UV out;
								out.uv0 = uv0;
								out.uv01[0] = ToF16((uv1 - uv0).getX());
								out.uv01[1] = ToF16((uv1 - uv0).getY());
								out.uv02[0] = ToF16((uv2 - uv0).getX());
								out.uv02[1] = ToF16((uv2 - uv0).getY());

								postProcessedUVs.emplace_back(std::move(out));
							}

							auto pbrMetallicRoughnessIt = material.find("pbrMetallicRoughness");
							if (pbrMetallicRoughnessIt != material.end())
							{
								const json& pbrMetallicRoughness = pbrMetallicRoughnessIt.value();

								int id = GetElementInt(pbrMetallicRoughness, "baseColorTexture/index", -1);
								if (id >= 0)
								{
									Texture* pTexture = pGLTFTexturesAndBuffers->GetTextureViewByID(id);

									auto find = std::find(m_alphaTextures.cbegin(), m_alphaTextures.cend(), pTexture);
									if (find == m_alphaTextures.cend())
									{
										textureIndex =(uint32_t) m_alphaTextures.size();
										m_alphaTextures.push_back(pTexture);
									}
									else
									{
										textureIndex = (uint32_t) (find - m_alphaTextures.cbegin());
									}
								}
							}
						}
					}
					const json& inAccessor = pGLTFTexturesAndBuffers->m_pGLTFCommon->m_pAccessors->at(attr);


					bool bUsingSkinning = pGLTFTexturesAndBuffers->m_pGLTFCommon->FindMeshSkinId(i) != -1;

					
					BLAS blas;
					blas.SetMaskParams(uvOffset, textureIndex);
					blas.AddGeometry(geometry, CAULDRON_DX12::GetFormat(inAccessor["type"], inAccessor["componentType"]), bIsOpaque);

					blas.PreBuild(pDevice);

					size_t size = blas.GetStructureSize();
					D3D12_GPU_VIRTUAL_ADDRESS address = 0;
					for (auto&& buffer : m_buffers)
					{
						address = buffer->Suballoc((uint32_t)size);
						if (address != 0)
						{
							break;
						}
					}

					if (address == 0)
					{
						uint32_t allocSize = 16 * 1024 * 1024; // 16MB pool
						if (size > allocSize)
						{
							allocSize = static_cast<uint32_t>(size);
						}
						ASBuffer* pNewPool = new ASBuffer();
						pNewPool->OnCreate(pDevice, allocSize, false, "BLAS buffer");

						address = pNewPool->Suballoc((uint32_t)size);
						m_buffers.push_back(pNewPool);
					}

					blas.AssignBuffer(address);

					m_meshes[i].structs[p] = m_structures.size();
					m_structures.push_back(blas);
				}
			}

			pResourceViewHeaps->AllocCBV_SRV_UAVDescriptor((uint32_t)m_alphaTextures.size(), &m_maskTextureTable);
			for (size_t i = 0; i < m_alphaTextures.size(); ++i)
			{
				m_alphaTextures[i]->CreateSRV((uint32_t)i, &m_maskTextureTable);
			}

			if (postProcessedUVs.size())
			{
				m_blasUVBuffer.InitBuffer(pDevice, "BLAS UV buffer", &CD3DX12_RESOURCE_DESC::Buffer(sizeof(UV) * postProcessedUVs.size()), sizeof(UV), D3D12_RESOURCE_STATE_COPY_DEST);
				pUpload->AddBufferCopy(postProcessedUVs.data(), (uint32_t)(sizeof(UV) * postProcessedUVs.size()), m_blasUVBuffer.GetResource());
			}
		}
	}

	TLAS ASFactory::BuildTLASFromGLTF(CAULDRON_DX12::Device* pDevice, GLTFTexturesAndBuffers* pGLTFTexturesAndBuffers, bool bGatherOpaque, bool bGatherNonOpaque)
	{
		// loop through nodes
	   //
		std::vector<tfNode> const& nodes = pGLTFTexturesAndBuffers->m_pGLTFCommon->m_nodes;
		Matrix2* pNodesMatrices = pGLTFTexturesAndBuffers->m_pGLTFCommon->m_worldSpaceMats.data();

		TLAS tlas;

		for (uint32_t i = 0; i < nodes.size(); i++)
		{
			tfNode const& node = nodes[i];
			if (node.meshIndex < 0)
				continue;

			math::Matrix4 mModelToWorld = pNodesMatrices[i].GetCurrent();

			Mesh mesh = m_meshes[node.meshIndex];
			for (uint32_t j = 0; j < mesh.structs.size(); j++)
			{
				BLAS& blas = m_structures[mesh.structs[j]];

				if ((blas.IsOpaque() && bGatherOpaque)
					|| (!blas.IsOpaque() && bGatherNonOpaque))
				{
					tlas.AddInstance(blas, mModelToWorld);
				}
			}
		}

		tlas.PreBuild(pDevice);

		size_t size = tlas.GetStructureSize();
		D3D12_GPU_VIRTUAL_ADDRESS address = m_tlasBuffer.Suballoc((uint32_t)size);
		tlas.AssignBuffer(address);

		return std::move(tlas);
	}

	void ASFactory::SyncTLASBuilds(ID3D12GraphicsCommandList* pCmdList)
	{
		D3D12_RESOURCE_BARRIER postBuild[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(m_tlasBuffer.GetResource()),
		};
		pCmdList->ResourceBarrier(1, postBuild);
	}

	void ASFactory::ClearBuiltStructures(void)
	{
		m_structures.clear();

		for (auto&& iter : m_buffers)
		{
			iter->OnDestroy();
		}
		m_buffers.clear();


		m_blasUVBuffer.OnDestroy();
		m_alphaTextures.clear();
	}

	void ASFactory::ResetTLAS(void)
	{
		m_tlasBuffer.Reset();
	}

	CBV_SRV_UAV& ASFactory::GetMaskTextureTable(void)
	{
		return m_maskTextureTable;
	}

	Texture* ASFactory::GetUVBuffer(void)
	{
		return &m_blasUVBuffer;
	}

	std::vector<BLAS>& ASFactory::GetBLASVector(void)
	{
		return m_structures;
	}
	ASBuffer::ASBuffer(void)
		: m_memOffset(0)
		, m_totalMemSize(0)
		, m_pBuffer(nullptr)
	{
	}

	ASBuffer::~ASBuffer(void)
	{
	}

	void ASBuffer::OnCreate(Device* pDevice, uint32_t totalMemSize, bool isScratch, const char* name)
	{
		m_totalMemSize = AlignUp(totalMemSize, (uint32_t)D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

		ThrowIfFailed(
			pDevice->GetDevice()->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(m_totalMemSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
				(isScratch) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
				nullptr,
				IID_PPV_ARGS(&m_pBuffer))
		);
		SetName(m_pBuffer, "ASBuffer::m_pBuffer");

	}

	void ASBuffer::OnDestroy()
	{
		if (m_pBuffer)
		{
			m_pBuffer->Release();
			m_pBuffer = nullptr;
		}

		m_memOffset = 0;
		m_totalMemSize = 0;

	}

	D3D12_GPU_VIRTUAL_ADDRESS ASBuffer::Suballoc(uint32_t byteSize)
	{
		uint32_t size = AlignUp(byteSize, (uint32_t)D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
		
		D3D12_GPU_VIRTUAL_ADDRESS address = 0;
		if (m_memOffset + size <= m_totalMemSize)
		{
			address = m_memOffset + m_pBuffer->GetGPUVirtualAddress();
			m_memOffset += size;
		}
		return address;
	}

	ID3D12Resource* ASBuffer::GetResource(void) const
	{
		return m_pBuffer;
	}

	void ASBuffer::Reset(void)
	{
		m_memOffset = 0;
	}

}