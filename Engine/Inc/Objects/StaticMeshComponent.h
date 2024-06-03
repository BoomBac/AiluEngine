#pragma warning(disable : 4251)
#pragma once
#ifndef __STATICMESH_COMP__
#define __STATICMESH_COMP__
#include <future>
#include "Component.h"
#include "Render/Mesh.h"
#include "Render/Material.h"
#include "Animation/Clip.h"

namespace Ailu
{
	class AILU_API StaticMeshComponent : public Component
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(StaticMeshComponent)
		DECLARE_REFLECT_FIELD(StaticMeshComponent)
	public:
		DISALLOW_COPY_AND_ASSIGN(StaticMeshComponent)
		StaticMeshComponent();
		StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat);
		void BeginPlay() override;
		void Tick(const float& delta_time) override;
		void OnGizmo() override;
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) override;
		virtual void SetMesh(Ref<Mesh>& mesh);
		void AddMaterial(Ref<Material> mat);
		void AddMaterial(Ref<Material> mat,u16 slot);
		void RemoveMaterial(u16 slot);
		void SetMaterial(Ref<Material>& mat,u16 slot = 0u);
		inline Ref<Material>& GetMaterial(u16 slot = 0) { return _p_mats[slot]; };
		inline Vector<Ref<Material>>& GetMaterials() { return _p_mats; };
		inline Ref<Mesh>& GetMesh() { return _p_mesh; };
		const auto& GetAABB() const
		{
			return _transformed_aabbs;
		}
	protected:
		Ref<Mesh> _p_mesh;
		Vector<Ref<Material>> _p_mats;
		Vector<AABB> _transformed_aabbs;
	protected:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
	};
	REFLECT_FILED_BEGIN(StaticMeshComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kStaticMesh, StaticMesh, _p_mesh)
	REFLECT_FILED_END

	class AILU_API SkinedMeshComponent : public StaticMeshComponent
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(SkinedMeshComponent)
		DECLARE_REFLECT_FIELD(SkinedMeshComponent)
	public:
		DISALLOW_COPY_AND_ASSIGN(SkinedMeshComponent)
		SkinedMeshComponent();
		SkinedMeshComponent(Ref<Mesh> mesh, Ref<Material> mat);
		void BeginPlay() override;
		void Tick(const float& delta_time) override;
		void OnGizmo() override;
		void Serialize(std::ostream& os, String indent) override;
		void SetMesh(Ref<Mesh>& mesh) final;
		void SetAnimationClip(Ref<AnimationClip>& clip) {_p_clip = clip;};
		AnimationClip* GetAnimationClip() {return _p_clip.get();};
		float _anim_time = 0.0f;
		bool _is_draw_debug_skeleton = true;
		bool _is_mt_skin = true;
		float _skin_time = 0.0f;
	private:
		inline constexpr static u16 s_skin_thread_num = 12u;
		Ref<AnimationClip> _p_clip;
		u16 _per_skin_task_vertex_num = 0u;
		//Vector<std::future<void>> _skin_tasks;
	private:
		float _pre_anim_time = 0.0f;
		void Skin(float time);
		void SkinTask(AnimationClip* clip, u32 time, Vector3f* vert, u32 begin, u32 end);
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
	};
	REFLECT_FILED_BEGIN(SkinedMeshComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kStaticMesh, StaticMesh, _p_mesh)
	REFLECT_FILED_END
}


#endif // !STATICMESH_COMP__

