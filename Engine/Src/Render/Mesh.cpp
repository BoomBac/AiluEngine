#include "pch.h"
#include "Render/Mesh.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/ThreadPool.h"
#include "Render/Gizmo.h"

namespace Ailu
{
	Mesh::Mesh()
	{
		_vertices = nullptr;
		_normals = nullptr;
		_colors = nullptr;
		_tangents = nullptr;
		_uv = new Vector2f * [8];
		for (size_t i = 0; i < 8; i++)
		{
			_uv[i] = nullptr;
		}
		_vertex_count = 0u;
		_index_count = 0u;
	}

	Mesh::Mesh(const std::string& name) : Mesh()
	{
		_name = name;
	}
	Mesh::~Mesh()
	{
		Clear();
		DESTORY_PTRARR(_uv)
	}
	void Mesh::SetVertices(Vector3f* vertices)
	{
		_vertices = vertices;
	}
	void Ailu::Mesh::SetNormals(Vector3f* normals)
	{
		_normals = normals;
	}
	void Mesh::SetTangents(Vector4f* tangents)
	{
		_tangents = tangents;
	}
	void Mesh::SetUVs(Vector2f* uv, uint8_t index)
	{
		if (index >= 8)
		{
			AL_ASSERT(true, "Uv index in mesh must be less than 8!");
			return;
		}
		_uv[index] = uv;
	}
	void Mesh::Clear()
	{
		DESTORY_PTRARR(_vertices);
		DESTORY_PTRARR(_normals);
		DESTORY_PTRARR(_colors);
		DESTORY_PTRARR(_tangents);
		if (_uv != nullptr)
		{
			for (size_t i = 0; i < 8; i++)
			{
				DESTORY_PTRARR(_uv[i]);
			}
		}
		_vertex_count = 0;
		_index_count = 0;
	}
	void Mesh::SetIndices(uint32_t* indices)
	{
		_p_indices = indices;
	}
	const Ref<VertexBuffer>& Mesh::GetVertexBuffer() const
	{
		return _p_vbuf;
	}
	const Ref<IndexBuffer>& Mesh::GetIndexBuffer() const
	{
		return _p_ibuf;
	}
	void Mesh::Build()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index;
		if (_vertices)
		{
			desc_list.push_back({ "POSITION",EShaderDateType::kFloat3,count });
			vert_index = count++;
		}
		if (_normals)
		{
			desc_list.push_back({ "NORMAL",EShaderDateType::kFloat3,count });
			normal_index = count++;
		}
		if (_uv[0])
		{
			desc_list.push_back({ "TEXCOORD",EShaderDateType::kFloat2,count });
			uv_index = count++;
		}
		if (_tangents)
		{
			desc_list.push_back({ "TANGENT",EShaderDateType::kFloat4,count });
			tangent_index = count++;
		}
		_p_vbuf.reset(VertexBuffer::Create(desc_list));
		if (_vertices) _p_vbuf->SetStream(reinterpret_cast<float*>(_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index);
		if (_normals) _p_vbuf->SetStream(reinterpret_cast<float*>(_normals), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index);
		if (_uv[0]) _p_vbuf->SetStream(reinterpret_cast<float*>(_uv[0]), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index);
		if (_tangents) _p_vbuf->SetStream(reinterpret_cast<float*>(_tangents), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index);
		_p_ibuf.reset(IndexBuffer::Create(_p_indices, _index_count));
	}

	//----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
	// Parallel 函数
	//template <typename T, typename F>
	//void Parallel(T* v, size_t size, F f, ThreadPool& threadPool)
	//{
	//	size_t numThreads = std::thread::hardware_concurrency();
	//	size_t chunkSize = (size + numThreads - 1) / numThreads;

	//	std::vector<std::future<void>> futures;

	//	for (size_t i = 0; i < numThreads; ++i)
	//	{
	//		size_t startIdx = i * chunkSize;
	//		size_t endIdx = std::min((i + 1) * chunkSize, size);

	//		futures.push_back(threadPool.Enqueue([&, startIdx, endIdx]()
	//			{
	//				for (size_t j = startIdx; j < endIdx; ++j) {
	//					f(v[j]);
	//				}
	//			}));
	//	}
	//	for (auto& future : futures)
	//	{
	//		future.wait();
	//	}
	//}
	
	SkinedMesh::SkinedMesh() : Mesh()
	{
		_bone_indices = nullptr;
		_bone_weights = nullptr;
	}

	SkinedMesh::SkinedMesh(const String& name) : SkinedMesh()
	{
		_name = name;
	}
	SkinedMesh::~SkinedMesh()
	{
		Clear();
		DESTORY_PTRARR(_uv)
	}

	void SkinedMesh::SetBoneWeights(Vector4f* bone_weights)
	{
		_bone_weights = bone_weights;
	}
	void SkinedMesh::SetBoneIndices(Vector4D<u32>* bone_indices)
	{
		_bone_indices = bone_indices;
	}

	void SkinedMesh::Skin()
	{
		float world_time = g_pTimeMgr->GetScaledWorldTime(1.0f);
		auto joints = _skeleton._joints;
		u64 time = (u32)world_time % joints[0]._frame_count;
		Vector3f* pos = reinterpret_cast<Vector3f*>(_p_vbuf->GetStream(0));
		static auto skin = [&](u32 begin,u32 end) {
			for (u32 i = begin; i < end; i++)
			{
				auto& cur_weight = _bone_weights[i];
				auto& cur_indices = _bone_indices[i];
				Vector3f tmp = _vertices[i];
				Vector3f new_pos = Vector3f::kZero;
				for (u16 j = 0; j < 4; j++)
				{
					auto& joint = _skeleton._joints[cur_indices[j]];
					new_pos += cur_weight[j] * TransformCoord(joint._inv_bind_pos * joint._pose[time], tmp);
				}
				pos[i] = new_pos;
			}
		};
		if (_vertex_count < 1000)
		{
			skin(0, _vertex_count);
		}
		else
		{
			size_t numThreads = 6;
			size_t chunkSize = (_vertex_count + numThreads - 1) / numThreads;
			std::vector<std::future<void>> futures;
			for (size_t i = 0; i < numThreads; ++i)
			{
				size_t startIdx = i * chunkSize;
				size_t endIdx = min((i + 1) * chunkSize, _vertex_count);
				futures.push_back(g_thread_pool->Enqueue(skin, startIdx, endIdx));
			}
			for (auto& future : futures)
			{
				future.wait();
			}
		}

	}
	void SkinedMesh::Clear()
	{
		Mesh::Clear();
		DESTORY_PTRARR(_bone_weights);
		DESTORY_PTRARR(_bone_indices);
	}

	void SkinedMesh::Build()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index, bone_index_index, bone_weight_index;
		if (_vertices)
		{
			desc_list.push_back({ "POSITION",EShaderDateType::kFloat3,count });
			vert_index = count++;
		}
		if (_normals)
		{
			desc_list.push_back({ "NORMAL",EShaderDateType::kFloat3,count });
			normal_index = count++;
		}
		if (_uv[0])
		{
			desc_list.push_back({ "TEXCOORD",EShaderDateType::kFloat2,count });
			uv_index = count++;
		}
		if (_tangents)
		{
			desc_list.push_back({ "TANGENT",EShaderDateType::kFloat4,count });
			tangent_index = count++;
		}
		if (_bone_indices)
		{
			desc_list.push_back({ "BONEINDEX",EShaderDateType::kInt4,count });
			bone_index_index = count++;
		}
		if (_bone_weights)
		{
			desc_list.push_back({ "BONEWEIGHT",EShaderDateType::kFloat4,count });
			bone_weight_index = count++;
		}
		_p_vbuf.reset(VertexBuffer::Create(desc_list));
		if (_vertices) _p_vbuf->SetStream(reinterpret_cast<u8*>(_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index, true);
		if (_normals) _p_vbuf->SetStream(reinterpret_cast<u8*>(_normals), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index);
		if (_uv[0]) _p_vbuf->SetStream(reinterpret_cast<u8*>(_uv[0]), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index);
		if (_tangents) _p_vbuf->SetStream(reinterpret_cast<u8*>(_tangents), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index);
		if (_bone_indices) _p_vbuf->SetStream(reinterpret_cast<u8*>(_bone_indices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kInt4), bone_index_index);
		if (_bone_weights) _p_vbuf->SetStream(reinterpret_cast<u8*>(_bone_weights), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), bone_weight_index);
		_p_ibuf.reset(IndexBuffer::Create(_p_indices, _index_count));
	}
	//----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
}
