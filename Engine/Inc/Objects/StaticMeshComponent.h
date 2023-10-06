#pragma once
#ifndef __STATICMESH_COMP__
#define __STATICMESH_COMP__
#include "Component.h"
#include "Framework/Assets/Mesh.h"
#include "Render/Material.h"

namespace Ailu
{
	class StaticMeshComponent : public Component
	{
		COMPONENT_CLASS_TYPE(StaticMeshComponent)
	public:
		StaticMeshComponent(Ref<Mesh>& mesh, Ref<Material>& mat);
		void BeginPlay() final;
		void Tick(const float& delta_time) final;
		void SetMesh(Ref<Mesh>& mesh);
		void SetMaterial(Ref<Material>& mat);
		inline Ref<Material>& GetMaterial() { return _p_mat; };
		inline Ref<Mesh>& GetMesh() { return _p_mesh; };
	private:
		Ref<Mesh> _p_mesh;
		Ref<Material> _p_mat;
	};
}


#endif // !STATICMESH_COMP__

