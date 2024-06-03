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
#ifdef COMPANY_ENV
			inline static const WString kEditorRootPathW = L"C:/AiluEngine/Editor/";
#else
			inline static const WString kEditorRootPathW = L"D:/ProjectCpp/AiluEngine/Editor/";
#endif
		public:
			int Initialize() final;
			void Finalize() final;
			void Tick(const float& delta_time) final;
		private:
			EditorLayer* _p_editor_layer;
			InputLayer* _p_input_layer;
			SceneLayer* _p_scene_layer;
			ApplicationDesc _desc;
			const WString kEditorConfigPath = kEditorRootPathW + L"EditorConfig.ini";
			Camera* _p_scene_camera;
			WString _opened_scene_path;
		private:
			bool OnGetFoucus(WindowFocusEvent& e) final;
			bool OnLostFoucus(WindowLostFocusEvent& e) final;
			void LoadEditorConfig(ApplicationDesc& desc);
			void SaveEditorConfig();
		};
	}
}

#endif // !EDITOR_APP__

