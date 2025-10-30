#include "pch.h"
#include "Render/Mesh.h"
#include "Render/GraphicsContext.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"

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
            return nullptr;
        }
        return _index_buffers[submesh_index];
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
