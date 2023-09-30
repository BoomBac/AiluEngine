#pragma once
#ifndef __STATICMESH_COMP__
#define __STATICMESH_COMP__
#include "Component.h"
#include "Framework/Assets/Mesh.h"

namespace Ailu
{
	class StaticMeshComponent : public Component
	{
	public:
		void BeginPlay() final;
		void Tick(const float& delta_time) final;
		COMPONENT_CLASS_TYPE(StaticMeshComponent)
	private:
		Ref<Mesh> _p_mesh;
	};
}


#endif // !STATICMESH_COMP__

