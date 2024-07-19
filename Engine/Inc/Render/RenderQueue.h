#pragma once
#ifndef __RENDER_QUEUE__
#define __RENDER_QUEUE__

#include "GlobalMarco.h"
#include "Material.h"
#include "Mesh.h"

namespace Ailu
{
	//struct RenderableObjectData
	//{
	//	u16 _mesh_index;
	//	u16 _material_index;
	//	u16 _transform_index;
	//	u16 _submesh_index;
	//	u32 _instance_count;
	//	Mesh* GetMesh() const;
	//	Material* GetMaterial() const;
	//	Matrix4x4f* GetTransform() const;
	//};

	//class RenderQueue
	//{
	//public:
	//	inline static constexpr u32 kQpaque = 2000;
	//	inline static constexpr u32 kTransparent = 3000;
	//	inline static constexpr u32 kShaderError = 4000;
	//	inline static constexpr u32 kShaderCompiling = 4001;
	//	static void Enqueue(u32 queue_id, Mesh* mesh, Material* material, Matrix4x4f transform, u16 submesh_index,u32 instance_count);
	//	static void ClearQueue();
	//	static const Vector<RenderableObjectData>& GetOpaqueRenderables() {return _s_renderables[kQpaque];};
	//	static const Vector<RenderableObjectData>& GetTransparentRenderables() {return _s_renderables[kTransparent];}
	//	static const Vector<RenderableObjectData>& GetErrorShaderRenderables() {return _s_renderables[kShaderError];}
	//	static const Vector<RenderableObjectData>& GetCompilingShaderRenderables() {return _s_renderables[kShaderCompiling];}
	//	static const std::map<u32, Vector<RenderableObjectData>>& GetAllRenderables() {return _s_renderables;}
	//	inline static Vector<Mesh*> s_all_meshes{};
	//	inline static Vector<Material*> s_all_materials{};
	//	inline static Vector<Matrix4x4f> s_all_matrix{};
	//private:
	//	inline static std::map<u32, Vector<RenderableObjectData>> _s_renderables{};
	//};
}

#endif // !RENDER_QUEUE__

