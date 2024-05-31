#pragma once
#ifndef __EDITOR_APP__
#define __EDITOR_APP__

#include "Ailu.h"
#include "Framework/Common/Application.h"
#include "Widgets/EditorLayer.h"

namespace Ailu
{
	class Camera;
	namespace Editor
	{
		class InputLayer;
		class SceneLayer;
		class EditorApp : public Ailu::Application
		{
		public:
			int Initialize() final;
			void Finalize() final;
			void Tick(const float& delta_time) final;
		private:
			EditorLayer* _p_editor_layer;
			InputLayer* _p_input_layer;
			SceneLayer* _p_scene_layer;
			ApplicationDesc _desc;
			const WString kEditorConfigPath = L"EditorConfig.ini";
			Scope<Camera> _p_scene_camera;
		private:
			bool OnGetFoucus(WindowFocusEvent& e) final;
			bool OnLostFoucus(WindowLostFocusEvent& e) final;
			void LoadEditorConfig();
		};
	}
}

#endif // !EDITOR_APP__

