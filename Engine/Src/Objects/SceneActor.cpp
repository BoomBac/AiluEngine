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
}
