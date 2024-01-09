#include "pch.h"
#include "Render/RenderQueue.h"

namespace Ailu
{
	void RenderQueue::Enqueue(u32 queue_id, Mesh* mesh, Material* material, Matrix4x4f transform, u32 instance_count)
	{
		auto it = _s_renderables.find(queue_id);
		if (it != _s_renderables.end())
		{
			it->second.emplace_back(mesh, material, transform, instance_count);
		}
		else
		{
			_s_renderables.insert(std::make_pair(queue_id, std::vector<RenderableObjectData>()));
			_s_renderables[queue_id].emplace_back(mesh, material, transform, instance_count);
		}
	}
	void RenderQueue::ClearQueue()
	{
		_s_renderables.clear();
	}
}
