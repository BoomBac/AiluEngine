#include "EditorApp.h"
#include "Widgets/OutputLog.h"
#include "Widgets/InputLayer.h"
#include "Widgets/SceneLayer.h"

#include "Render/Renderer.h"
#include "Render/Camera.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"

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
			LoadEditorConfig(desc);
			auto ret = Application::Initialize(desc);
			_p_input_layer = new InputLayer();
			PushLayer(_p_input_layer);
			_p_scene_layer = new SceneLayer();
			PushLayer(_p_scene_layer);
			g_pLogMgr->AddAppender(new ImGuiLogAppender());
			_p_editor_layer = new EditorLayer();
			PushLayer(_p_editor_layer);
			g_pRenderer->Initialize(desc._gameview_width, desc._gameview_height);
			{
				g_pSceneMgr->_p_current = g_pResourceMgr->Load<Scene>(_opened_scene_path);
			}
			return ret;
		}
		void EditorApp::Finalize()
		{
			SaveEditorConfig();
			delete _p_scene_camera;
			Application::Finalize();
			fs::path p(kEditorRootPathW);
			fs::directory_iterator dir_it(p);

			List<WString> deleted_wpix_files;
			for (auto it : dir_it)
			{
				if (su::EndWith(it.path().string(), ".wpix"))
				{
					deleted_wpix_files.emplace_back(it.path().wstring());
				}
			}
			for (auto& dp : deleted_wpix_files)
				FileManager::DeleteDirectory(dp);
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
		void EditorApp::LoadEditorConfig(ApplicationDesc& desc)
		{
			//auto work_dir = PathUtils::ExtarctDirectory(Application::GetWorkingPath());
			String configs;
			FileManager::ReadFile(kEditorConfigPath, configs);
			Map<String, String> config_pairs;
			auto config_lines = su::Split(configs,"\n");
			for (auto line : config_lines)
			{
				String f_line = line; 
				su::RemoveSpaces(f_line);
				auto v = su::Split(f_line, "=");
				if (v.size() != 2)
					continue;
				config_pairs[v[0]] = v[1];
			}
			_p_scene_camera = new Camera();
			Camera::sCurrent = _p_scene_camera;
			Vector2f v2;
			Vector3f v3;
			Vector4f v4;
			LoadVector(config_pairs["Position"].c_str(), v3);
			_p_scene_camera->Position(v3);
			LoadVector(config_pairs["Rotation"].c_str(), v4);
			_p_scene_camera->Rotation(v4);
			_p_scene_camera->FovH(LoadFloat(config_pairs["Fov"].c_str()));
			_p_scene_camera->Aspect(LoadFloat(config_pairs["Aspect"].c_str()));
			_p_scene_camera->Near(LoadFloat(config_pairs["Near"].c_str()));
			_p_scene_camera->Far(LoadFloat(config_pairs["Far"].c_str()));
			LoadVector(config_pairs["ControllerRotation"].c_str(), v2);
			FirstPersonCameraController::s_instance._rotation = v2;
			_p_scene_camera->RecalculateMarix(true);
			_opened_scene_path = ToWStr(config_pairs["Scene"].c_str());

			LoadVector(config_pairs["WindowSize"].c_str(), v2);
			desc._window_width = v2.x;
			desc._window_height = v2.y;
			LoadVector(config_pairs["ViewPortSize"].c_str(), v2);
			desc._gameview_width = v2.x;
			desc._gameview_height = v2.y;
		}
		void EditorApp::SaveEditorConfig()
		{
			Camera::sCurrent = _p_scene_camera;
			using std::endl;
			std::stringstream ss;
			ss << "[Common]" << std::endl;
			ss << "WindowSize = " << Vector2f(_p_window->GetWidth(), _p_window->GetHeight()) << endl;
			ss << "ViewPortSize = " << _p_editor_layer->_p_render_view->_size << endl;
			ss << "[SceneCamera]" << std::endl;
			ss << "Type = " << ECameraType::ToString(Camera::sCurrent->Type()) <<std::endl;
			ss << "Position = " << Camera::sCurrent->Position() << endl;
			ss << "Rotation = " << Camera::sCurrent->Rotation() << endl;
			ss << "Fov = " << Camera::sCurrent->FovH() << endl;
			ss << "Aspect = " << Camera::sCurrent->Aspect() << endl;
			ss << "Near = " << Camera::sCurrent->Near() << endl;
			ss << "Far = " << Camera::sCurrent->Far() << endl;
			ss << "ControllerRotation = " << FirstPersonCameraController::s_instance._rotation << endl;
			ss << "[Scene]" << endl;
			ss << "Scene = " << ToChar(g_pResourceMgr->GetAssetPath(g_pSceneMgr->_p_current));
			FileManager::WriteFile(kEditorConfigPath,false,ss.str());
		}
	}
}
