#include "EditorApp.h"
#include "Widgets/OutputLog.h"
#include "Widgets/InputLayer.h"
#include "Widgets/SceneLayer.h"

#include "Render/Renderer.h"
#include "Render/Camera.h"
#include "Framework/Common/FileManager.h"

using namespace Ailu;

namespace Ailu
{
	namespace Editor
	{
		int EditorApp::Initialize()
		{
			ApplicationDesc desc;
			desc._window_width = 1600;
			desc._window_height = 900;
			desc._gameview_width = 1600;
			desc._gameview_height = 900;
			LoadEditorConfig();
			auto ret = Application::Initialize();

			_p_input_layer = new InputLayer();
			PushLayer(_p_input_layer);
			_p_scene_layer = new SceneLayer();
			PushLayer(_p_scene_layer);
			g_pLogMgr->AddAppender(new ImGuiLogAppender());
			_p_editor_layer = new EditorLayer();
			PushLayer(_p_editor_layer);
			g_pRenderer->Initialize(desc._gameview_width, desc._gameview_height);
			return ret;
		}
		void EditorApp::Finalize()
		{
			Application::Finalize();
		}
		void EditorApp::Tick(const float& delta_time)
		{
			Application::Tick(delta_time);
		}
		bool EditorApp::OnGetFoucus(WindowFocusEvent& e)
		{
			Application::OnGetFoucus(e);
			_p_input_layer->HandleInput(true);
			return true;
		}
		bool EditorApp::OnLostFoucus(WindowLostFocusEvent& e)
		{
			Application::OnLostFoucus(e);
			_p_input_layer->HandleInput(false);
			return true;
		}
		void EditorApp::LoadEditorConfig()
		{
			auto work_dir = PathUtils::ExtarctDirectory(Application::GetWorkingPath());
			WString configs;
			FileManager::ReadFile(work_dir + kEditorConfigPath, configs);
			Map<WString, WString> config_pairs;
			auto config_lines = su::Split(configs,L"\\n");
			for (auto line : config_lines)
			{
				WString f_line = line; 
				su::RemoveSpaces(f_line);
				auto v = su::Split(f_line, L"=");
				AL_ASSERT(v.size() != 2);
				config_pairs[v[0]] = v[2];
			}
			_p_scene_camera = MakeScope<Camera>();
			Camera::sCurrent = _p_scene_camera.get();
			_p_scene_camera->
		}
	}
}
