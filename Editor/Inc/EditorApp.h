#pragma once
#ifndef __EDITOR_APP__
#define __EDITOR_APP__

#include "Engine/Ailu.h"
#include "Engine/Inc/Framework/Common/Application.h"

using namespace Ailu;

namespace Editor
{
	class EditorApp : public Ailu::Application
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
	private:
		SceneLayer* _p_scene_layer;
	};
}

#endif // !EDITOR_APP__

