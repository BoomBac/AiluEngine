#pragma once
#ifndef __RENDER_QUEUE__
#define __RENDER_QUEUE__

#include "GlobalMarco.h"
#include "Material.h"
#include "Mesh.h"

namespace Ailu
{
	struct RenderableObjectData
	{
		Mesh* _mesh;
		Material* _mat;
		Matrix4x4f _transform;
		u32 _instance_count;
	};

	class RenderQueue
	{
	public:
		inline static constexpr u32 kQpaque = 2000;
		inline static constexpr u32 kTransparent = 3000;
		static void Enqueue(u32 queue_id, Mesh* mesh, Material* material, Matrix4x4f transform, u32 instance_count = 1);
		static void ClearQueue();
		static const Vector<RenderableObjectData>& GetOpaqueRenderables() {return _s_renderables[kQpaque];};
		static const Vector<RenderableObjectData>& GetTransparentRenderables() {return _s_renderables[kTransparent];}
	private:
		inline static std::map<u32, Vector<RenderableObjectData>> _s_renderables{};
	};
}

#endif // !RENDER_QUEUE__

