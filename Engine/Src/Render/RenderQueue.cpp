#include "pch.h"
#include "Render/RenderQueue.h"

namespace Ailu
{
	void RenderQueue::Enqueue(u32 queue_id, Mesh* mesh, Material* material, Matrix4x4f transform, u16 submesh_index, u32 instance_count)
	{
		auto it = _s_renderables.find(queue_id);
		s_all_materials.emplace_back(material);
		s_all_meshes.emplace_back(mesh);
		s_all_matrix.emplace_back(transform);
		if (it != _s_renderables.end())
		{
			it->second.emplace_back(RenderableObjectData(s_all_meshes.size() - 1, s_all_materials.size() - 1, s_all_matrix.size() - 1, submesh_index,instance_count));
		}
		else
		{
			_s_renderables.insert(std::make_pair(queue_id, std::vector<RenderableObjectData>()));
			_s_renderables[queue_id].emplace_back(RenderableObjectData(s_all_meshes.size() - 1, s_all_materials.size() - 1, s_all_matrix.size() - 1, submesh_index,instance_count));
		}
	}

	void RenderQueue::ClearQueue()
	{
		s_all_materials.clear();
		s_all_meshes.clear();
		s_all_matrix.clear();
		_s_renderables.clear();
	}
	Mesh* RenderableObjectData::GetMesh() const
	{
		return RenderQueue::s_all_meshes[_mesh_index];
	}
	Material* RenderableObjectData::GetMaterial() const
	{
		return RenderQueue::s_all_materials[_material_index];
	}
	Matrix4x4f* RenderableObjectData::GetTransform() const
	{
		return &RenderQueue::s_all_matrix[_transform_index];
	}
}
