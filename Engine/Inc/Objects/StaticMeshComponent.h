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
	class StaticMeshComponent : public Component
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(StaticMeshComponent)
		DECLARE_REFLECT_FIELD(StaticMeshComponent)
	public:
		StaticMeshComponent();
		StaticMeshComponent(Ref<Mesh> mesh, Ref<Material> mat);
		void BeginPlay() override;
		void Tick(const float& delta_time) override;
		void OnGizmo() override;
		virtual void SetMesh(Ref<Mesh>& mesh);
		void SetMaterial(Ref<Material>& mat);
		void Serialize(std::basic_ostream<char, std::char_traits<char>>& os, String indent) override;
		inline Ref<Material>& GetMaterial() { return _p_mat; };
		inline Ref<Mesh>& GetMesh() { return _p_mesh; };
		const AABB& GetAABB() const
		{
			return _aabb;
		}
	protected:
		Ref<Mesh> _p_mesh;
		Ref<Material> _p_mat;
		AABB _aabb;
	protected:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
	};
	REFLECT_FILED_BEGIN(StaticMeshComponent)
	DECLARE_REFLECT_PROPERTY(ESerializablePropertyType::kStaticMesh, StaticMesh, _p_mesh)
	REFLECT_FILED_END

	class SkinedMeshComponent : public StaticMeshComponent
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
		COMPONENT_CLASS_TYPE(SkinedMeshComponent)
		DECLARE_REFLECT_FIELD(SkinedMeshComponent)
	public:
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
		Vector<std::future<void>> _skin_tasks;
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

