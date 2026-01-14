#include "pch.h"
#include "Render/Mesh.h"
#include "Render/GraphicsContext.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu::Render
{
#pragma region Mesh
	Mesh::Mesh()
	{
		_vertex_count = 0u;
	}

	Mesh::Mesh(String name) : Mesh()
	{
		_name = name;
	}
	Mesh::~Mesh()
	{
		Clear();
	}
	void Mesh::Clear()
	{
		_vertices.clear();
		_normals.clear();
		_colors.clear();
		_tangents.clear();
		for (auto &uv: _uvs)
			uv.clear();
		_submeshes.clear();
		_bounds.clear();
		_imported_taterials.clear();
		_vertex_count = 0;
		//TODO:release gpu data...
	}
	void Mesh::SetVertices(std::span<const Vector3f> vertices)
	{
		_vertex_count = static_cast<u32>(vertices.size());
		_vertices.assign(vertices.begin(), vertices.end());
	}
	void Mesh::SetVertices(Vector<Vector3f> &&vertices)
	{
		_vertex_count = static_cast<u32>(vertices.size());
		_vertices = std::move(vertices);
	}
	void Mesh::SetNormals(std::span<const Vector3f> normals)
	{
		_normals.assign(normals.begin(), normals.end());
	}
	void Mesh::SetNormals(Vector<Vector3f> &&normals)
	{
		_normals = std::move(normals);
	}
	void Mesh::SetTangents(std::span<const Vector4f> tangents)
	{
		_tangents.assign(tangents.begin(), tangents.end());
	}
	void Mesh::SetTangents(Vector<Vector4f> &&tangents)
	{
		_tangents = std::move(tangents);
	}
	void Mesh::SetColors(std::span<const Color> colors)
	{
		_colors.assign(colors.begin(), colors.end());
	}
	void Mesh::SetColors(Vector<Color> &&colors)
	{
		_colors = std::move(colors);
	}
	void Mesh::AddSubmesh(std::span<const u32> indices)
	{
		Vector<u32> index_data;
		index_data.assign(indices.begin(), indices.end());
		_submeshes.emplace_back(index_data);
	}
	std::span<const u32> Mesh::GetIndices(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16)_submeshes.size())
		{
			LOG_ERROR("Mesh::GetIndices: mesh({}) submesh({}) is invalid!", _name, submesh_index);
			return std::span<const u32>();
		}
		return std::span<const u32>(_submeshes[submesh_index]._indices);
	}
	i32 Mesh::GetIndicesCount(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16)_submeshes.size())
		{
			LOG_ERROR("Mesh::GetIndicesCount: mesh({}) submesh({}) is invalid!", _name, submesh_index);
			return -1;
		}
		return (i32)_submeshes[submesh_index]._indices.size();
	}
	const Ref<IndexBuffer> &Mesh::GetIndexBuffer(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16) _index_buffers.size())
		{
			LOG_ERROR("Mesh::GetIndicesCount: mesh({}) submesh({}) is invalid!", _name, submesh_index);
			return _index_buffers[0];
		}
		return _index_buffers[submesh_index];
	}

	static void CalculateTriangleBounds(u64 start_triangle_index, u64 end_triangle_index, const Vector<Vector3f>& vertices, 
		const Vector<Vector3f>& normals,const Vector<Vector2f>& uv0,const Vector<u32>& indices, Vector<AABB>& triangle_bounds,Vector<TriangleData>& triangle_data)
	{
		for (u64 i = start_triangle_index; i < end_triangle_index; i++)
		{
			u32 index0 = indices[i * 3 + 0];
			u32 index1 = indices[i * 3 + 1];
			u32 index2 = indices[i * 3 + 2];
			Vector3f v0 = vertices[index0];
			Vector3f v1 = vertices[index1];
			Vector3f v2 = vertices[index2];
			AABB aabb = AABB::Infinity();
			AABB::Encapsulate(aabb, v0);
			AABB::Encapsulate(aabb, v1);
			AABB::Encapsulate(aabb, v2);
			triangle_bounds[i] = aabb;
			triangle_data[i].v0 = v0;
			triangle_data[i].v1 = v1;
			triangle_data[i].v2 = v2;
			if (normals.size())
			{
				triangle_data[i].n0 = normals[index0];
				triangle_data[i].n1 = normals[index1];
				triangle_data[i].n2 = normals[index2];
			}
			if (uv0.size())
			{
				triangle_data[i].uv0 = uv0[index0];
				triangle_data[i].uv1 = uv0[index1];
				triangle_data[i].uv2 = uv0[index2];
			}
		}
	}

	void Mesh::GenerateTriangleBounds()
	{
		if (_vertices.empty())
			return;
		Vector<u32> flat_indices;
		for (auto &sm: _submeshes)
			flat_indices.insert(flat_indices.end(), sm._indices.begin(), sm._indices.end());
        _triangle_count = (u32) (flat_indices.size() / 3u);
		_triangle_data.resize(_triangle_count);
		_triangle_bounds.resize(_triangle_count);
		const static u64 kTriNumPerJob = 800;
		auto &job_sys = JobSystem::Get();
		Vector<WaitHandle> wait_handles;
		{
            TIMER_BLOCK(std::format("Mesh::Generate Bounds and BVH({})", _name))
            //for (u64 i = 0; i < _triangle_count; i += kTriNumPerJob)
            //{
            //	u64 index_end = std::min<u64>(i + kTriNumPerJob, _triangle_count);
            //	wait_handles.push_back(job_sys.Dispatch(CalculateTriangleBounds, i, index_end, std::ref(_vertices), std::ref(_normals),std::ref(_uvs[0]),
            //	std::ref(flat_indices), std::ref(_triangle_bounds),std::ref(_triangle_data)));
            //}
            //for (auto &handle: wait_handles)
            //{
            //	job_sys.Wait(handle);
            //}
            CalculateTriangleBounds(0, _triangle_count, _vertices, _normals, _uvs[0],flat_indices, _triangle_bounds, _triangle_data);
			BVHBuilder builder(_triangle_bounds);
			auto&& result = builder.Build();
			_bvh_nodes = std::move(result._nodes);
			Vector<TriangleData> reordered;
			reordered.resize(_triangle_data.size());
			for (u32 new_idx = 0; new_idx < result._reordered_indices.size(); new_idx++)
			{
				u32 old_idx = result._reordered_indices[new_idx];
				reordered[new_idx] = _triangle_data[old_idx];
			}
			_triangle_data = std::move(reordered);
            Vector<AABB> tri_bounds_reordered;
            tri_bounds_reordered.resize(_triangle_bounds.size());
			for (u32 new_idx = 0; new_idx < result._reordered_indices.size(); new_idx++)
			{
				u32 old_idx = result._reordered_indices[new_idx];
				tri_bounds_reordered[new_idx] = _triangle_bounds[old_idx];
            }
            _triangle_bounds = std::move(tri_bounds_reordered);
		}
	}
	void Mesh::SetUVs(std::span<const Vector2f> uv, u8 channel)
	{
		if (channel >= kMaxUVChannels)
		{
			LOG_ERROR("Mesh::SetUVs: mesh({}) channel({}) is invalid!", _name, channel);
			return;
		}
		if (channel > 1)
		{
			LOG_WARNING("Mesh::SetUVs: mesh({}) only support one uv channel", _name);
			return;
		}
		_uvs[channel].assign(uv.begin(), uv.end());
	}
	void Mesh::SetUVs(Vector<Vector2f> &&uv, u8 channel)
	{
		if (channel >= kMaxUVChannels)
		{
			LOG_ERROR("Mesh::SetUVs: mesh({}) channel({}) is invalid!", _name, channel);
			return;
		}
		if (channel > 1)
		{
			LOG_WARNING("Mesh::SetUVs: mesh({}) only support one uv channel", _name);
			return;
		}
		_uvs[channel] = std::move(uv);
	}
	void Mesh::Apply()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index;
		if (_vertices.size())
		{
			desc_list.push_back({ "POSITION",EShaderDateType::kFloat3,count });
			vert_index = count++;
		}
		if (_normals.size())
		{
			desc_list.push_back({ "NORMAL",EShaderDateType::kFloat3,count });
			normal_index = count++;
		}
		if (_uvs[0].size())
		{
			desc_list.push_back({ "TEXCOORD",EShaderDateType::kFloat2,count });
			uv_index = count++;
		}
		if (_tangents.size())
		{
			desc_list.push_back({ "TANGENT",EShaderDateType::kFloat4,count });
			tangent_index = count++;
		}
		if (!desc_list.empty())
		{
			_vertex_buffer.reset(VertexBuffer::Create(desc_list, _name));
			if (_vertices.size()) 
				_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_vertices.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index, false);
			if (_normals.size()) 
				_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_normals.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index, false);
			if (_uvs[0].size()) 
				_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_uvs[0].data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index, false);
			if (_tangents.size()) 
				_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_tangents.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index, false);
			_vertex_buffer->Name(_name);
			GraphicsContext::Get().CreateResource(_vertex_buffer.get());
		}
		_index_buffers.resize(_submeshes.size());
		for (int i = 0; i < _submeshes.size(); i++)
		{
			auto &submesh = _submeshes[i];
			if (submesh._indices.size())
			{
				_index_buffers[i].reset(IndexBuffer::Create(submesh._indices.data(), (u32)submesh._indices.size()));
				String ib_name = std::format("{}_{}", _name, i);
				_index_buffers[i]->Name(ib_name);
				GraphicsContext::Get().CreateResource(_index_buffers[i].get());
			}
		}
		GenerateTriangleBounds();
	}
#pragma endregion

#pragma region SkeletonMesh
	//----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
	SkeletonMesh::SkeletonMesh() : Mesh()
	{

	}

	SkeletonMesh::SkeletonMesh(const String& name) : SkeletonMesh()
	{
		_name = name;
	}
	SkeletonMesh::~SkeletonMesh()
	{
		Clear();
	}

	void SkeletonMesh::Clear()
	{
		Mesh::Clear();
		_bone_weights.clear();
		_bone_indices.clear();
		_previous_vertices.clear();
	}

	void SkeletonMesh::SetBoneWeights(std::span<const Vector4f> bone_weights)
	{
		_bone_weights.assign(bone_weights.begin(), bone_weights.end());
	}

	void SkeletonMesh::SetBoneIndices(std::span<const Vector4D<u32>> bone_indices)
	{
		_bone_indices.assign(bone_indices.begin(), bone_indices.end());
	}

	void SkeletonMesh::Apply()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index,prev_vert_index;
		if (_vertices.size())
		{
			desc_list.push_back({ "POSITION",EShaderDateType::kFloat3,count });
			vert_index = count++;
		}
		if (_normals.size())
		{
			desc_list.push_back({ "NORMAL",EShaderDateType::kFloat3,count });
			normal_index = count++;
		}
		if (_uvs[0].size())
		{
			desc_list.push_back({ "TEXCOORD",EShaderDateType::kFloat2,count });
			uv_index = count++;
		}
		if (_tangents.size())
		{
			desc_list.push_back({ "TANGENT",EShaderDateType::kFloat4,count });
			tangent_index = count++;
		}
		//is for gpu skinning
		//if (_bone_indices)
		//{
		//	desc_list.push_back({ "BONEINDEX",EShaderDateType::kInt4,count });
		//	bone_index_index = count++;
		//}
		//if (_bone_weights)
		//{
		//	desc_list.push_back({ "BONEWEIGHT",EShaderDateType::kFloat4,count });
		//	bone_weight_index = count++;
		//}
		{
			desc_list.push_back({ "TEXCOORD",EShaderDateType::kFloat3,count ,1});
			prev_vert_index = count++;
		}
		_vertex_buffer.reset(VertexBuffer::Create(desc_list, _name));
		if (_vertices.size()) 
		{
			_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_vertices.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index, true);
			_previous_vertices.assign(_vertices.begin(), _vertices.end());
		}
		if (_normals.size()) _vertex_buffer->SetStream(reinterpret_cast<u8 *>(_normals.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index, true);
		if (_uvs[0].size()) _vertex_buffer->SetStream(reinterpret_cast<u8 *>(_uvs[0].data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index, false);
		if (_tangents.size()) _vertex_buffer->SetStream(reinterpret_cast<u8 *>(_tangents.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index, false);
		//if (_bone_indices) _p_vbuf->SetStream(reinterpret_cast<u8*>(_bone_indices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kInt4), bone_index_index);
		//if (_bone_weights) _p_vbuf->SetStream(reinterpret_cast<u8*>(_bone_weights), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), bone_weight_index);
		_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_previous_vertices.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), tangent_index, true);
		GraphicsContext::Get().CreateResource(_vertex_buffer.get());
		_index_buffers.resize(_submeshes.size());
		_triangle_count = 0u;
		for (int i = 0; i < _submeshes.size(); i++)
		{
			auto &submesh = _submeshes[i];
			if (submesh._indices.size())
			{
				_triangle_count += (u32) submesh._indices.size();
				_index_buffers[i].reset(IndexBuffer::Create(submesh._indices.data(), (u32)submesh._indices.size()));
				_index_buffers[i]->Name(std::format("{}_{}", _name, i));
				GraphicsContext::Get().CreateResource(_index_buffers[i].get());
			}
		}
		_triangle_count /= 3u;
		if (_vertices.size())
		{
			_triangle_data.resize(_triangle_count);
			u64 tri_index = 0u;
			for (u64 i = 0u; i < _submeshes.size(); i++)
			{
				const auto &indices = GetIndices((u16)i);
				for (u64 j = 0; j + 2 < indices.size(); j += 3)
				{
					_triangle_data[tri_index].v0 = _vertices[indices[j + 0]];
					_triangle_data[tri_index].v1 = _vertices[indices[j + 1]];
					_triangle_data[tri_index].v2 = _vertices[indices[j + 2]];
					_triangle_data[tri_index].n0 = _normals[indices[j + 0]];
					_triangle_data[tri_index].n1 = _normals[indices[j + 1]];
					_triangle_data[tri_index].n2 = _normals[indices[j + 2]];
					_triangle_data[tri_index].uv0 = _uvs[0][indices[j + 0]];
					_triangle_data[tri_index].uv1 = _uvs[0][indices[j + 1]];
					_triangle_data[tri_index].uv2 = _uvs[0][indices[j + 2]];
					++tri_index;
				}
			}
		}
	}
	void SkeletonMesh::SetSkeleton(const Skeleton &skeleton)
	{
		_skeleton = skeleton;
	}
	Skeleton &SkeletonMesh::GetSkeleton()
	{
		return _skeleton;
	}
#pragma endregion
	//----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
}
