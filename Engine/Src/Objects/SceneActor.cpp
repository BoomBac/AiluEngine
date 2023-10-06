#include "pch.h"
#include "Objects/SceneActor.h"

namespace Ailu
{
	SceneActor::SceneActor()
	{
		_p_transform = AddComponent<TransformComponent>();
	}
	SceneActor::~SceneActor()
	{

	}
	Transform& SceneActor::GetTransform()
	{
		return _p_transform->_transform;
	}
}
