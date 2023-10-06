#pragma once
#ifndef __SCENE_ACTOR_H__
#define __SCENE_ACTOR_H__
#include "Actor.h"
#include "TransformComponent.h"
namespace Ailu
{
	class SceneActor : public Actor
	{

	public:
		SceneActor();
		~SceneActor();
		Transform& GetTransform();
	private:
		TransformComponent* _p_transform = nullptr;
	};
}

#endif // !SCENE_ACTOR_H__

