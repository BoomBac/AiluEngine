#include "pch.h"
#include "Render/Mesh.h"
#include "Animation/SkeletonAsset.h"
#include "Render/GraphicsContext.h"
#include "Assets/Asset.h"
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
		_triangle_count = 0u;
		_derived_data_ready = false;
		_normal_stream = -1;
		_tangent_stream = -1;
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
	void Mesh::SetBounds(std::span<const AABB> bounds)
	{
		_bounds.assign(bounds.begin(), bounds.end());
	}
	void Mesh::SetDerivedData(Vector<AABB> &&triangle_bounds, Vector<TriangleData> &&triangle_data,
		Vector<BVHNode> &&bvh_nodes, Vector<Vector2UInt> &&bvh_node_ranges, u32 triangle_count)
	{
		_triangle_bounds = std::move(triangle_bounds);
		_triangle_data = std::move(triangle_data);
		_bvh_nodes = std::move(bvh_nodes);
		_submesh_bvh_node_ranges = std::move(bvh_node_ranges);
		_triangle_count = triangle_count;
		_derived_data_ready = true;
	}
	void Mesh::AddSubmesh(std::span<const u32> indices)
	{
		Vector<u32> index_data;
		index_data.assign(indices.begin(), indices.end());
		_submeshes.emplace_back(std::move(index_data));
	}
	void Mesh::AddSubmesh(Vector<u32> &&indices)
	{
		_submeshes.emplace_back(std::move(indices));
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
        i32 Mesh::GetBindlessVertexStreamIndex(EVertexSemantic semantic) const noexcept
        {
                return GetBindlessVertexStreamIndex(semantic, _vertex_buffer.get());
        }
        i32 Mesh::GetBindlessVertexStreamIndex(EVertexSemantic semantic,
                const VertexBuffer *vertex_buffer) const noexcept
        {
                if (_vertex_buffer == nullptr || vertex_buffer == nullptr)
                        return -1;

                for (const auto &desc : _vertex_buffer->GetLayout().GetBufferDesc())
                {
                        if (desc._semantic == semantic)
                        {
                                if (auto *gpu_stream = vertex_buffer->GetGpuStream(desc.Stream); gpu_stream != nullptr)
                                        return gpu_stream->GetBindlessSRVIndex();
                                return vertex_buffer->GetBindlessSRVIndex(desc.Stream);
                        }
                }
                return -1;
        }
	u32 Mesh::GetTriangleStart(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16)_submeshes.size())
			return 0u;

		u32 triangle_start = 0u;
		for (u16 index = 0u; index < submesh_index; ++index)
			triangle_start += static_cast<u32>(_submeshes[index]._indices.size() / 3u);
		return triangle_start;
	}
	u32 Mesh::GetTriangleCount(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16)_submeshes.size())
			return 0u;
		return static_cast<u32>(_submeshes[submesh_index]._indices.size() / 3u);
	}
	u32 Mesh::GetBVHNodeStart(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16)_submesh_bvh_node_ranges.size())
			return 0u;
		return _submesh_bvh_node_ranges[submesh_index].x;
	}
	u32 Mesh::GetBVHNodeCount(u16 submesh_index) const noexcept
	{
		if (submesh_index >= (u16)_submesh_bvh_node_ranges.size())
			return 0u;
		return _submesh_bvh_node_ranges[submesh_index].y;
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
		_triangle_count = 0u;
		for (const auto &submesh : _submeshes)
			_triangle_count += static_cast<u32>(submesh._indices.size() / 3u);
		_triangle_data.clear();
		_triangle_bounds.clear();
		_bvh_nodes.clear();
		_submesh_bvh_node_ranges.clear();
		_triangle_data.reserve(_triangle_count);
		_triangle_bounds.reserve(_triangle_count);
		_submesh_bvh_node_ranges.resize(_submeshes.size(), Vector2UInt::kZero);
		{
            TIMER_BLOCK(std::format("Mesh::Generate Bounds and BVH({})", _name))
			u32 global_triangle_offset = 0u;
			for (u16 submesh_index = 0u; submesh_index < static_cast<u16>(_submeshes.size()); ++submesh_index)
			{
				auto &submesh = _submeshes[submesh_index];
				const u32 triangle_count = static_cast<u32>(submesh._indices.size() / 3u);
				if (triangle_count == 0u)
					continue;

				Vector<AABB> local_triangle_bounds(triangle_count);
				Vector<TriangleData> local_triangle_data(triangle_count);
				CalculateTriangleBounds(0, triangle_count, _vertices, _normals, _uvs[0], submesh._indices, local_triangle_bounds, local_triangle_data);

				BVHBuilder builder(local_triangle_bounds);
				auto result = builder.Build();

				Vector<u32> reordered_indices(submesh._indices.size());
				for (u32 new_idx = 0u; new_idx < result._reordered_indices.size(); ++new_idx)
				{
					const u32 old_idx = result._reordered_indices[new_idx];
					_triangle_data.push_back(local_triangle_data[old_idx]);
					_triangle_bounds.push_back(local_triangle_bounds[old_idx]);

					const u32 old_base = old_idx * 3u;
					const u32 new_base = new_idx * 3u;
					reordered_indices[new_base + 0u] = submesh._indices[old_base + 0u];
					reordered_indices[new_base + 1u] = submesh._indices[old_base + 1u];
					reordered_indices[new_base + 2u] = submesh._indices[old_base + 2u];
				}
				submesh._indices = std::move(reordered_indices);

				_submesh_bvh_node_ranges[submesh_index] = Vector2UInt{ static_cast<u32>(_bvh_nodes.size()), static_cast<u32>(result._nodes.size()) };
				_bvh_nodes.insert(_bvh_nodes.end(), result._nodes.begin(), result._nodes.end());
				global_triangle_offset += triangle_count;
			}
		}
		_derived_data_ready = true;
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
	void Mesh::BuildDerivedData()
	{
		GenerateTriangleBounds();
	}

	void Mesh::Apply()
	{
		if (!_derived_data_ready)
			BuildDerivedData();
		UploadGpuResources();
	}

	void Mesh::UploadGpuResources()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index;
		_normal_stream = -1;
		_tangent_stream = -1;
                        if (_vertices.size())
		{
			desc_list.push_back({ EVertexSemantic::kPosition, EShaderDateType::kFloat3, count });
			vert_index = count++;
		}
		if (_normals.size())
		{
			desc_list.push_back({ EVertexSemantic::kNormal, EShaderDateType::kFloat3, count });
			normal_index = count++;
			_normal_stream = normal_index;
		}
		if (_uvs[0].size())
		{
			desc_list.push_back({ EVertexSemantic::kTexcoord0, EShaderDateType::kFloat2, count });
			uv_index = count++;
		}
		if (_tangents.size())
		{
			desc_list.push_back({ EVertexSemantic::kTangent, EShaderDateType::kFloat4, count });
			tangent_index = count++;
			_tangent_stream = tangent_index;
		}
		if (!desc_list.empty())
		{
			_vertex_buffer = VertexBuffer::Create(desc_list, _name);
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
				_index_buffers[i] = IndexBuffer::Create(submesh._indices.data(), (u32)submesh._indices.size());
				String ib_name = std::format("{}_{}", _name, i);
				_index_buffers[i]->Name(ib_name);
				GraphicsContext::Get().CreateResource(_index_buffers[i].get());
			}
		}
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
        _skeleton_asset.Clear();
	}

	void SkeletonMesh::SetBoneWeights(std::span<const Vector4f> bone_weights)
	{
		_bone_weights.assign(bone_weights.begin(), bone_weights.end());
	}

	void SkeletonMesh::SetBoneWeights(Vector<Vector4f> &&bone_weights)
	{
		_bone_weights = std::move(bone_weights);
	}

	void SkeletonMesh::SetBoneIndices(std::span<const Vector4D<u32>> bone_indices)
	{
		_bone_indices.assign(bone_indices.begin(), bone_indices.end());
	}

	void SkeletonMesh::SetBoneIndices(Vector<Vector4D<u32>> &&bone_indices)
	{
		_bone_indices = std::move(bone_indices);
	}

	bool SkeletonMesh::RemapBoneIndices(std::span<const u16> bone_remap)
	{
		for (auto &indices : _bone_indices)
		{
			for (u32 influence = 0u; influence < 4u; ++influence)
			{
				const u32 source_index = indices[influence];
				if (source_index >= bone_remap.size() || bone_remap[source_index] == Joint::kInvalidJointIndex)
					return false;
				indices[influence] = bone_remap[source_index];
			}
		}
		return true;
	}

	void SkeletonMesh::SetMeshBindGlobalTransform(const Matrix4x4f &transform)
	{
		_mesh_bind_global = transform;
		_mesh_current_global_inv = Math::MatrixInverse(transform);
	}

	void SkeletonMesh::RestoreBindPoseVertices()
	{
		if (_vertex_buffer == nullptr || _vertices.empty())
			return;
		memcpy(_vertex_buffer->GetStream(0), _vertices.data(), _vertices.size() * sizeof(Vector3f));
	}

	void SkeletonMesh::BuildSkinMatrixPalette(std::span<const Matrix4x4f> global_pose_palette,
	                                             Vector<Matrix4x4f> &out_palette) const
	{
		out_palette.clear();
		if (!_skeleton_asset.IsResolved())
			return;
		const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
		if (global_pose_palette.size() != skeleton.JointNum())
			return;
		out_palette.resize(skeleton.JointNum());
		for (const Joint &joint : skeleton)
		{
			out_palette[joint._self] = _mesh_bind_global * joint._inv_bind_pos *
				global_pose_palette[joint._self] * _mesh_current_global_inv;
		}
	}

	void SkeletonMesh::BuildMeshSpaceJointPositions(std::span<const Matrix4x4f> global_pose_palette,
	                                                 Vector<Vector3f> &out_positions) const
	{
		out_positions.clear();
		if (!_skeleton_asset.IsResolved())
			return;
		const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
		if (global_pose_palette.size() != skeleton.JointNum())
			return;
		out_positions.resize(skeleton.JointNum());
		for (const Joint &joint : skeleton)
			out_positions[joint._self] = TransformCoord(_mesh_current_global_inv,
				TransformCoord(global_pose_palette[joint._self], Vector3f::kZero));
	}

	void SkeletonMesh::Apply()
	{
		if (!_derived_data_ready)
			BuildDerivedData();
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index = 0u, normal_index = 0u, uv_index = 0u, tangent_index = 0u, prev_vert_index = 0u;
		u8 bone_index_index = 0u, bone_weight_index = 0u;
		_normal_stream = -1;
		_tangent_stream = -1;
		if (_vertices.size())
		{
			desc_list.push_back({ EVertexSemantic::kPosition, EShaderDateType::kFloat3, count });
			vert_index = count++;
		}
		if (_normals.size())
		{
			desc_list.push_back({ EVertexSemantic::kNormal, EShaderDateType::kFloat3, count });
			normal_index = count++;
			_normal_stream = normal_index;
		}
		if (_uvs[0].size())
		{
			desc_list.push_back({ EVertexSemantic::kTexcoord0, EShaderDateType::kFloat2, count });
			uv_index = count++;
		}
		if (_tangents.size())
		{
			desc_list.push_back({ EVertexSemantic::kTangent, EShaderDateType::kFloat4, count });
			tangent_index = count++;
			_tangent_stream = tangent_index;
		}
		if (!_bone_indices.empty())
		{
			desc_list.push_back({ EVertexSemantic::kBoneIndex, EShaderDateType::kuInt4, count });
			bone_index_index = count++;
		}
		if (!_bone_weights.empty())
		{
			desc_list.push_back({ EVertexSemantic::kBoneWeight, EShaderDateType::kFloat4, count });
			bone_weight_index = count++;
		}
		{
			desc_list.push_back({ EVertexSemantic::kTexcoord1, EShaderDateType::kFloat3, count });
			prev_vert_index = count++;
		}
		_vertex_buffer = VertexBuffer::Create(desc_list, _name);
		if (_vertices.size()) 
		{
			_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_vertices.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index, true);
			_previous_vertices.assign(_vertices.begin(), _vertices.end());
		}
		if (_normals.size()) _vertex_buffer->SetStream(reinterpret_cast<u8 *>(_normals.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index, true);
		if (_uvs[0].size()) _vertex_buffer->SetStream(reinterpret_cast<u8 *>(_uvs[0].data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index, false);
		if (_tangents.size()) _vertex_buffer->SetStream(reinterpret_cast<u8 *>(_tangents.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index, true);
		if (!_bone_indices.empty())
			_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_bone_indices.data()),
				_vertex_count * ShaderDateTypeSize(EShaderDateType::kuInt4), bone_index_index, false);
		if (!_bone_weights.empty())
			_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_bone_weights.data()),
				_vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), bone_weight_index, false);
		_vertex_buffer->SetStream(reinterpret_cast<u8 *>(_previous_vertices.data()), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), prev_vert_index, true);
		GraphicsContext::Get().CreateResource(_vertex_buffer.get());
		_index_buffers.resize(_submeshes.size());
		_triangle_count = 0u;
		for (int i = 0; i < _submeshes.size(); i++)
		{
			auto &submesh = _submeshes[i];
			if (submesh._indices.size())
			{
				_triangle_count += (u32) submesh._indices.size();
			_index_buffers[i] = IndexBuffer::Create(submesh._indices.data(), (u32)submesh._indices.size());
				_index_buffers[i]->Name(std::format("{}_{}", _name, i));
				GraphicsContext::Get().CreateResource(_index_buffers[i].get());
			}
		}
		_triangle_count /= 3u;
		if (_vertices.size() && !_derived_data_ready)
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
#pragma endregion
	//----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
}
