#include "pch.h"
#include "Render/Mesh.h"
#include "Render/GraphicsContext.h"
#include "Framework/Common/Asset.h"
#include "Framework/Common/Utils.h"

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
	void Mesh::SetUVs(Vector2f* uv, u8 index)
	{
		if (index >= 8)
		{
			AL_ASSERT_MSG(false, "Uv index in mesh must be less than 8!");
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
		for (auto indices : _p_indices)
		{
			DESTORY_PTRARR(std::get<0>(indices));
		}
		_p_indices.clear();
		_imported_materials.clear();
		_vertex_count = 0;
	}

	const Ref<VertexBuffer>& Mesh::GetVertexBuffer() const
	{
		return _p_vbuf;
	}
	const Ref<IndexBuffer>& Mesh::GetIndexBuffer(u16 submesh_index) const
	{
		return _p_ibufs[submesh_index];
	}
	void Mesh::AddSubmesh(u32* indices, u32 indices_count)
	{
		if(indices_count > 0)
			_p_indices.emplace_back(std::make_tuple(indices, indices_count));
	}
	void Mesh::Apply()
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
		if (!desc_list.empty())
		{
			_p_vbuf.reset(VertexBuffer::Create(desc_list, _name));
			if (_vertices) _p_vbuf->SetStream(reinterpret_cast<u8*>(_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index,false);
			if (_normals) _p_vbuf->SetStream(reinterpret_cast<u8*>(_normals), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index,false);
			if (_uv[0]) _p_vbuf->SetStream(reinterpret_cast<u8*>(_uv[0]), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index,false);
			if (_tangents) _p_vbuf->SetStream(reinterpret_cast<u8*>(_tangents), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index,false);
			_p_vbuf->Name(_name);
            GraphicsContext::Get().CreateResource(_p_vbuf.get());
		}
		_p_ibufs.resize(_p_indices.size());
		for(int i = 0; i < _p_indices.size(); i++)
		{
			if(std::get<1>(_p_indices[i]) != 0)
				_p_ibufs[i].reset(IndexBuffer::Create(std::get<0>(_p_indices[i]), std::get<1>(_p_indices[i])));
			String ib_name = std::format("{}_{}",_name,i);
			_p_ibufs[i]->Name(ib_name);
            GraphicsContext::Get().CreateResource(_p_ibufs[i].get());
		}
	}

	//----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
	SkeletonMesh::SkeletonMesh() : Mesh()
	{
		_bone_indices = nullptr;
		_bone_weights = nullptr;
	}

	SkeletonMesh::SkeletonMesh(const String& name) : SkeletonMesh()
	{
		_name = name;
	}
	SkeletonMesh::~SkeletonMesh()
	{
		Clear();
		DESTORY_PTRARR(_uv)
	}

	void SkeletonMesh::SetBoneWeights(Vector4f* bone_weights)
	{
		_bone_weights = bone_weights;
	}
	void SkeletonMesh::SetBoneIndices(Vector4D<u32>* bone_indices)
	{
		_bone_indices = bone_indices;
	}

	void SkeletonMesh::Clear()
	{
		Mesh::Clear();
		DESTORY_PTRARR(_bone_weights);
		DESTORY_PTRARR(_bone_indices);
		DESTORY_PTRARR(_previous_vertices);
	}

	void SkeletonMesh::Apply()
	{
		u8 count = 0;
		Vector<VertexBufferLayoutDesc> desc_list;
		u8 vert_index, normal_index, uv_index, tangent_index,prev_vert_index;
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
		_p_vbuf.reset(VertexBuffer::Create(desc_list, _name));
		if (_vertices) 
		{
			_p_vbuf->SetStream(reinterpret_cast<u8*>(_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), vert_index, true);
			_previous_vertices = new Vector3f[_vertex_count];
			memcpy(_previous_vertices, _vertices, _vertex_count * sizeof(Vector3f));
		}
		if (_normals) _p_vbuf->SetStream(reinterpret_cast<u8*>(_normals), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), normal_index,true);
		if (_uv[0]) _p_vbuf->SetStream(reinterpret_cast<u8*>(_uv[0]), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat2), uv_index,false);
		if (_tangents) _p_vbuf->SetStream(reinterpret_cast<u8*>(_tangents), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), tangent_index,false);
		//if (_bone_indices) _p_vbuf->SetStream(reinterpret_cast<u8*>(_bone_indices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kInt4), bone_index_index);
		//if (_bone_weights) _p_vbuf->SetStream(reinterpret_cast<u8*>(_bone_weights), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat4), bone_weight_index);
		_p_vbuf->SetStream(reinterpret_cast<u8*>(_previous_vertices), _vertex_count * ShaderDateTypeSize(EShaderDateType::kFloat3), tangent_index,true);
		GraphicsContext::Get().CreateResource(_p_vbuf.get());
        _p_ibufs.resize(_p_indices.size());
		for (int i = 0; i < _p_indices.size(); i++)
		{
			_p_ibufs[i].reset(IndexBuffer::Create(std::get<0>(_p_indices[i]), std::get<1>(_p_indices[i])));
            GraphicsContext::Get().CreateResource(_p_ibufs[i].get());
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
    //----------------------------------------------------------------------SkinedMesh---------------------------------------------------------------------------
}
