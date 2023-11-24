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
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(StaticMeshComponent)
		DECLARE_REFLECT_FIELD(StaticMeshComponent)
	public:
		StaticMeshComponent();
		StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat);
		void BeginPlay() final;
		void Tick(const float& delta_time) final;
		void SetMesh(Ref<Mesh>& mesh);
		void SetMaterial(Ref<Material>& mat);
		void Serialize(std::ofstream& file, String indent) final;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent);
		inline Ref<Material>& GetMaterial() { return _p_mat; };
		inline Ref<Mesh>& GetMesh() { return _p_mesh; };
	private:
		Ref<Mesh> _p_mesh;
		Ref<Material> _p_mat;
	private:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) final;
	};
	REFLECT_FILED_BEGIN(StaticMeshComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kStaticMesh, StaticMesh, _p_mesh)
	REFLECT_FILED_END
}


#endif // !STATICMESH_COMP__

