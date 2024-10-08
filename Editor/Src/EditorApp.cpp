#include "EditorApp.h"
#include "Common/Undo.h"
#include "Widgets/InputLayer.h"
#include "Widgets/OutputLog.h"
#include "Widgets/SceneLayer.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Camera.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/Renderer.h"

using namespace Ailu;

namespace Ailu
{
    namespace Editor
    {
        CommandManager *g_pCommandMgr = new CommandManager;
        int EditorApp::Initialize()
        {
            auto work_path = GetWorkingPath();
            LOG_INFO(L"WorkPath: {}", work_path);
            //AlluEngine/
            WString prex_w = work_path.substr(0, work_path.find(L"bin"));
            ResourceMgr::ConfigRootPath(prex_w);
            s_editor_root_path = prex_w + L"Editor/";
            s_editor_config_path = prex_w + L"Editor/EditorConfig.ini";

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
            {
                //g_pResourceMgr->Load<Scene>(_opened_scene_path);
                g_pSceneMgr->OpenScene(_opened_scene_path);
            }
            _pipeline.reset(new CommonRenderPipeline());
            g_pGfxContext->RegisterPipeline(_pipeline.get());
            _p_editor_layer = new EditorLayer();
            PushLayer(_p_editor_layer);
            _is_playing_mode = false;
            _is_simulate_mode = false;
            return ret;
        }
        void EditorApp::Finalize()
        {
            SaveEditorConfig();
            delete _p_scene_camera;
            DESTORY_PTR(g_pCommandMgr);
            _pipeline.release();
            Application::Finalize();
            fs::path p(s_editor_root_path);
            fs::directory_iterator dir_it(p);

            List<WString> deleted_wpix_files;
            for (auto it: dir_it)
            {
                if (su::EndWith(it.path().string(), ".wpix"))
                {
                    deleted_wpix_files.emplace_back(it.path().wstring());
                }
            }
            for (auto &dp: deleted_wpix_files)
                FileManager::DeleteDirectory(dp);
        }
        void EditorApp::Tick(f32 delta_time)
        {
            Application::Tick(delta_time);
        }
        bool EditorApp::OnGetFocus(WindowFocusEvent &e)
        {
            Application::OnGetFocus(e);
            return true;
        }
        bool EditorApp::OnLostFocus(WindowLostFocusEvent &e)
        {
            Application::OnLostFocus(e);
            return true;
        }
        void EditorApp::LoadEditorConfig(ApplicationDesc &desc)
        {
            //auto work_dir = PathUtils::ExtarctDirectory(Application::GetWorkingPath());
            String configs;
            FileManager::ReadFile(s_editor_config_path, configs);
            Map<String, String> config_pairs;
            auto config_lines = su::Split(configs, "\n");
            for (auto line: config_lines)
            {
                String f_line = line;
                su::RemoveSpaces(f_line);
                auto v = su::Split(f_line, "=");
                if (v.size() != 2)
                    continue;
                config_pairs[v[0]] = v[1];
            }
            _p_scene_camera = new Camera();
            _p_scene_camera->Name("SceneCamera");
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
            FirstPersonCameraController::s_inst._rotation = v2;
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
            ss << "WindowSize = " << Vector2f((f32) _p_window->GetWidth(), (f32) _p_window->GetHeight()) << endl;
            ss << "ViewPortSize = " << _p_editor_layer->_p_scene_view->Size() << endl;
            ss << "[SceneCamera]" << std::endl;
            ss << "Type = " << ECameraType::ToString(Camera::sCurrent->Type()) << std::endl;
            ss << "Position = " << Camera::sCurrent->Position() << endl;
            ss << "Rotation = " << Camera::sCurrent->Rotation() << endl;
            ss << "Fov = " << Camera::sCurrent->FovH() << endl;
            ss << "Aspect = " << Camera::sCurrent->Aspect() << endl;
            ss << "Near = " << Camera::sCurrent->Near() << endl;
            ss << "Far = " << Camera::sCurrent->Far() << endl;
            ss << "ControllerRotation = " << FirstPersonCameraController::s_inst._rotation << endl;
            ss << "[Scene]" << endl;
            ss << "Scene = " << ToChar(g_pResourceMgr->GetAssetPath(g_pSceneMgr->ActiveScene())) << endl;//;
            FileManager::WriteFile(s_editor_config_path, false, ss.str());
        }
    }// namespace Editor
}// namespace Ailu
