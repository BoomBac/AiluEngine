#pragma once
#ifndef __SCENE_ACTOR_H__
#define __SCENE_ACTOR_H__
#include "Actor.h"
#include "Framework/Math/Geometry.h"
#include "TransformComponent.h"
namespace Ailu
{
	class AILU_API SceneActor : public Actor
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
	public:
        inline static f32 s_global_aabb_scale = 1.0f;
        inline static const f32 kBasAABBInitialSize = 1.0f;
		DISALLOW_COPY_AND_ASSIGN(SceneActor)
		SceneActor();
		~SceneActor();
		void Tick(const float& delta_time) override;
		virtual void OnGizmo();
		const Transform& GetTransform() const { return _p_transform->SelfTransform(); }
		TransformComponent* GetTransformComponent() { return _p_transform; }
		void Serialize(std::ostream& os, String indent) override;
        const AABB& BaseAABB() const {return _base_aabb;}
		void IsActive(bool active) {_is_active = active;};
		bool IsActive() const {return _is_active;};
		//SceneActor& operator=(const SceneActor& other)
		//{
		//	return *this;
		//}
	protected:
        //for pick
        AABB _base_aabb;
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
	private:
		TransformComponent* _p_transform = nullptr;
		bool _is_active = true;
	};
}

#endif // !SCENE_ACTOR_H__

