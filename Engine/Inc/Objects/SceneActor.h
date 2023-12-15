#pragma once
#ifndef __SCENE_ACTOR_H__
#define __SCENE_ACTOR_H__
#include "Actor.h"
#include "TransformComponent.h"
namespace Ailu
{
	class SceneActor : public Actor
	{
		template<class T>
		friend static T* Deserialize(Queue<std::tuple<String, String>>& formated_str);
	public:
		SceneActor();
		~SceneActor();
		Transform& GetTransform();
		void Serialize(std::ofstream& file, String indent) override;
		void Serialize(std::ostream& os, String indent) override;
		SceneActor& operator=(const SceneActor& other)
		{
			return *this;
		}
	protected:
		void* DeserializeImpl(Queue<std::tuple<String, String>>& formated_str) override;
	private:
		TransformComponent* _p_transform = nullptr;
	};
}

#endif // !SCENE_ACTOR_H__

