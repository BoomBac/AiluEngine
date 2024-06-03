#pragma once
#ifndef __SCENE_ACTOR_H__
#define __SCENE_ACTOR_H__
#include "Actor.h"
#include "TransformComponent.h"
namespace Ailu
{
	class AILU_API SceneActor : public Actor
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
	public:
		DISALLOW_COPY_AND_ASSIGN(SceneActor)
		SceneActor();
		~SceneActor();
		void Tick(const float& delta_time) override;
		virtual void OnGizmo();
		const Transform& GetTransform() const { return _p_transform->SelfTransform(); }
		TransformComponent* GetTransformComponent() { return _p_transform; }
		void Serialize(std::ostream& os, String indent) override;
		//SceneActor& operator=(const SceneActor& other)
		//{
		//	return *this;
		//}
	protected:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
	private:
		TransformComponent* _p_transform = nullptr;
	};
}

#endif // !SCENE_ACTOR_H__

