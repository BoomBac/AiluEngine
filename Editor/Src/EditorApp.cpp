#include "EditorApp.h"
#include "Automation/AIAssistant.h"
#include "Automation/AutomationAdapter.h"
#include "Automation/AutomationAssetService.h"
#include "Automation/AutomationDestructiveService.h"
#include "Automation/AutomationPipeServer.h"
#include "Automation/AutomationReadModel.h"
#include "Automation/AutomationReflectionService.h"
#include "Automation/AutomationSceneService.h"
#include "Automation/AutomationService.h"
#include "Automation/AutomationSession.h"
#include "Automation/AutomationWriteService.h"
#include "Common/Selection.h"
#include "Common/Undo.h"
#include "Widgets/InputLayer.h"
#include "Widgets/RenderView.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorRegistration.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/Type.h"
#include "Render/Camera.h"
#include "Render/AssetPreviewGenerator.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/Renderer.h"

#include "Framework/Parser/TextParser.h"
#include "Objects/JsonArchive.h"

#include "Common/CameraControllers.h"
#include "Common/EditorStyle.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/Style/UITheme.h"
#include "UI/Widget.h"
#include "Project/ProjectManager.h"

using namespace Ailu;

namespace Ailu
{
    using namespace Render;
    namespace Editor
    {
        namespace fs = std::filesystem;

        CommandManager *g_pCommandMgr = new CommandManager;

        EditorStyle g_editor_style = {};
        UI::UITheme g_editor_ui_theme = {};

        EditorApp::EditorApp()
        {
        }

        EditorApp::~EditorApp()
        {
        }
        int EditorApp::Initialize()
        {
            return Initialize(ApplicationInitContext{});
        }

        int EditorApp::Initialize(ApplicationInitContext init_ctx)
        {
            LogMgr::Init();
            Enum::InitTypeInfo();

            Application::SetEngineConfigPath(Application::GetUserHomePath() + L"/OneDrive/AiluEngine/Editor/EngineConfig.json");
            s_editor_root_path = Application::GetUserHomePath() + L"/OneDrive/AiluEngine/Editor/";
            s_editor_config_path = Application::GetUserHomePath() + L"/OneDrive/AiluEngine/Editor/EditorConfig.json";
            ApplicationDesc desc;
            desc._window_width = 1600;
            desc._window_height = 900;
            desc._gameview_width = 1600;
            desc._gameview_height = 900;
            _camera_controller = MakeScope<FirstPersonCameraController>();
            LoadEditorConfig(desc);
            auto ret = Application::Initialize(desc,init_ctx);
            _p_input_layer = new InputLayer();
            PushLayer(_p_input_layer);
            _pipeline.reset(new CommonRenderPipeline());
            Render::RenderPipeline::Register(_pipeline.get());
            {
                Selection::RemoveSlection();
                SceneManagement::SceneMgr::Get().OpenScene(_opened_scene_path);
            }
            LoadEditorResource();
            ResourceMgr::Get().MigrateLegacyAssetDocuments();
            g_editor_style = DefaultDark();
            {
                JsonArchive ar;
                fs::path theme_path = fs::path(s_editor_root_path) / L"Res/UI/EditorStyle.json";
                ar.Load(theme_path);
                if (ar.IsLoaded())
                    ar >> g_editor_style;
            }
            g_editor_ui_theme = UI::UITheme::DefaultDark();
            {
                JsonArchive ar;
                fs::path theme_path = fs::path(s_editor_root_path) / L"Res/UI/UITheme_Dark.json";
                ar.Load(theme_path);
                if (ar.IsLoaded())
                    ar >> g_editor_ui_theme;
                UI::UIManager::Get()->SetTheme(&g_editor_ui_theme);
            }
            RegisterComponentEditors();
            _automation_service = MakeScope<EditorAutomationService>();
            _automation_service->Initialize();
            {
                RegisterDefaultComponentDescriptors();
                AutomationAdapterRegistry::Get().Initialize();
                AutomationSceneService::Register(_automation_service->Registry());
                AutomationAssetService::Register(_automation_service->Registry());
                AutomationReflectionService::Register(_automation_service->Registry());
                AutomationWriteService::Register(_automation_service->Registry());
                AutomationDestructiveService::Register(_automation_service->Registry());
            }
            {
                // Built-in AI assistant: talks to the automation services in-process.
                AIAssistantService::Get().SetRegistry(&_automation_service->Registry());
                AIAssistantService::Get().SetProvider(MakeScope<DemoAIProvider>());
            }
            _pipe_server = MakeScope<AutomationPipeServer>();
            _pipe_server->Initialize(*_automation_service);
            if (ProjectManager::Get().HasOpenedProject())
            {
                AutomationSessionInfo session_info;
                session_info._pid = GetCurrentProcessId();
                session_info._session_id = _pipe_server->SessionId();
                session_info._pipe_name = _pipe_server->PipeName();
                session_info._project_path = ToChar(ProjectManager::Get().CurrentProject().RootDirectory());
                session_info._editor_version = "0.1.0";
                AutomationSession::Save(ProjectManager::Get().CurrentProject().RootDirectory(), session_info);
            }
            _p_editor_layer = new EditorLayer();
            PushLayer(_p_editor_layer);
            _is_playing_mode = false;
            _is_simulate_mode = false;
            ConfigureResourceReloading();
            return ret;
        }
        void EditorApp::Finalize()
        {
            SaveEditorConfig();
            AssetPreviewGenerator::Shutdown();
            if (_pipe_server)
                _pipe_server->Finalize();
            if (_automation_service)
                _automation_service->Finalize();
            delete _p_scene_camera;
            delete g_pCommandMgr; g_pCommandMgr = nullptr;
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
            LogMgr::Shutdown();
        }
        void EditorApp::Tick(f32 delta_time)
        {
            // Application::Tick enters the main loop and does not return, so the
            // automation service is drained per frame from EditorLayer::OnUpdate.
            Application::Tick(delta_time);
        }
        bool EditorApp::OnGetFocus(WindowFocusEvent &e)
        {
            Application::OnGetFocus(e);
            WatchDirectory();
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
            JsonArchive ar;
            ar.Load(s_editor_config_path);
            const Type *type = EditorConfig::StaticType();
            for (auto &it: type->GetProperties())
                it.Deserialize(&_editor_config, ar);
            //INIParser parser;
            //parser.Load(s_editor_config_path);
            //const auto &config_values = parser.GetValues();
            //for (auto &it: type->GetProperties())
            //{
            //    if (auto itt = config_values.find(it.Name()); itt != config_values.end())
            //        it.SetValueFromString(&_editor_config, itt->second);
            //}
            _p_scene_camera = new Camera();
            _p_scene_camera->_anti_aliasing = EAntiAliasing::kNone;
            _p_scene_camera->Name("SceneCamera");
            _p_scene_camera->Position(_editor_config._position);
            _p_scene_camera->Rotation(_editor_config._rotation);
            _p_scene_camera->FovH(_editor_config._fov);
            _p_scene_camera->Aspect(_editor_config._aspect);
            _p_scene_camera->Near(_editor_config._near);
            _p_scene_camera->Far(_editor_config._far);
            Camera::sScene = _p_scene_camera;
            Camera::sCurrent = _p_scene_camera;
            _camera_controller->_rotation = _editor_config._controller_rot;
            _camera_controller->_base_camera_move_speed = _editor_config._move_speed;
            _p_scene_camera->RecalculateMatrix(true);
            _opened_scene_path = ToWChar(_editor_config._scene_path);
            desc._window_width = _editor_config._window_size.x;
            desc._window_height = _editor_config._window_size.y;
            desc._gameview_width = _editor_config._viewport_size.x;
            desc._gameview_height = _editor_config._viewport_size.y;
        }
        void EditorApp::SaveEditorConfig()
        {
            Camera::sCurrent = _p_scene_camera;
            _editor_config._window_size = Vector2UInt(_p_window->GetWidth(), _p_window->GetHeight());
            //_editor_config._viewport_size = Vector2UInt((u32)_p_editor_layer->_p_scene_view->Size().x, (u32)_p_editor_layer->_p_scene_view->Size().y);
            _editor_config._position = Camera::sCurrent->Position();
            _editor_config._rotation = Vector4f(Camera::sCurrent->Rotation().x, Camera::sCurrent->Rotation().y, Camera::sCurrent->Rotation().z, Camera::sCurrent->Rotation().w);
            _editor_config._fov = Camera::sCurrent->FovH();
            _editor_config._aspect = Camera::sCurrent->Aspect();
            _editor_config._near = Camera::sCurrent->Near();
            _editor_config._far = Camera::sCurrent->Far();
            _editor_config._move_speed = _camera_controller->_base_camera_move_speed;
            _editor_config._controller_rot = _camera_controller->_rotation;
            _editor_config._scene_path = ToChar(ResourceMgr::Get().GetAssetPath(SceneManagement::SceneMgr::Get().ActiveScene()));

            JsonArchive ar;
            const Type *type = EditorConfig::StaticType();
            for (auto &it: type->GetProperties())
                it.Serialize(&_editor_config, ar);
            ar.Save(s_editor_config_path);

            //INIParser ini_parser;
            //
            //for (auto &prop: EditorConfig::StaticType()->GetProperties())
            //{
            //    ini_parser.SetValue(prop.MetaInfo()._category, prop.Name(), prop.StringValue(&_editor_config));
            //}
            //ini_parser.Save(s_editor_config_path);
        }
        void EditorApp::LoadEditorResource()
        {
            TimerBlock t("LoadEditorResource");
            TextureImportSetting color_tex_setting, normal_tex_setting;
            color_tex_setting._generate_mipmap = false;
            normal_tex_setting._is_sRGB = false;
            auto& job_sys = JobSystem::Get();
            Vector<WString> texture_sys_path = {
                    EnginePath::kEngineIconPathW + L"folder.alasset",
                    EnginePath::kEngineIconPathW + L"file.alasset",
                    EnginePath::kEngineIconPathW + L"3d.alasset",
                    EnginePath::kEngineIconPathW + L"shader.alasset",
                    EnginePath::kEngineIconPathW + L"image.alasset",
                    EnginePath::kEngineIconPathW + L"dark/material.alasset",
                    EnginePath::kEngineIconPathW + L"dark/scene.alasset",
                    EnginePath::kEngineIconPathW + L"point_light.alasset",
                    EnginePath::kEngineIconPathW + L"directional_light.alasset",
                    EnginePath::kEngineIconPathW + L"spot_light.alasset",
                    EnginePath::kEngineIconPathW + L"area_light.alasset",
                    EnginePath::kEngineIconPathW + L"camera.alasset",
                    EnginePath::kEngineIconPathW + L"light_probe.alasset",
                    EnginePath::kEngineIconPathW + L"dark/anim_clip.alasset",
                    EnginePath::kEngineIconPathW + L"dark/skeleton.alasset",
                    EnginePath::kEngineTexturePathW + L"ibl_brdf_lut.alasset",
                    EnginePath::kEngineTexturePathW + L"T_Default_Material_Grid_M.alasset"
                };
            for (auto &path: texture_sys_path)
            {
                job_sys.Dispatch([path, color_tex_setting]()
                                          { ResourceMgr::Get().Load<Texture2D>(path, &color_tex_setting); });
            }
            job_sys.Dispatch([normal_tex_setting]()
                            { ResourceMgr::Get().Load<Texture2D>(EnginePath::kEngineTexturePathW + L"T_Default_Material_Grid_N.alasset", &normal_tex_setting); });
            job_sys.Wait();
            auto mat_creator = [this](const WString &shader_path, const WString &mat_path, const String &mat_name) -> Material *
            {
                auto mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(shader_path), mat_name);
                ResourceMgr::Get().RegisterResource(mat_path, mat);
                return mat.get();
            };
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/PointLightBillboard", "PointLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/DirectionalLightBillboard", "DirectionalLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/SpotLightBillboard", "SpotLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/AreaLightBillboard", "AreaLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/CameraBillboard", "CameraBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/LightProbeBillboard", "LightProbeBillboard");
            ResourceMgr::Get().Get<Material>(L"Runtime/Material/PointLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"point_light.alasset");
            ResourceMgr::Get().Get<Material>(L"Runtime/Material/DirectionalLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"directional_light.alasset");
            ResourceMgr::Get().Get<Material>(L"Runtime/Material/SpotLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"spot_light.alasset");
            ResourceMgr::Get().Get<Material>(L"Runtime/Material/AreaLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"area_light.alasset");
            ResourceMgr::Get().Get<Material>(L"Runtime/Material/CameraBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"camera.alasset");
            ResourceMgr::Get().Get<Material>(L"Runtime/Material/LightProbeBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"light_probe.alasset");
            mat_creator(L"Shaders/hlsl/plane_grid.hlsl", L"Runtime/Material/GridPlane", "GridPlane")->SetFloat("_grid_alpha", 1.0f);
            Material::s_checker = ResourceMgr::Get().Load<Material>(EnginePath::kEngineMaterialPathW + L"M_Default.alasset");
        }

        void EditorApp::ConfigureResourceReloading()
        {
            if (_is_resource_reload_configured)
                return;

            _resource_reload_service.SetEngineConfigPath(s_engine_config_path);
            _resource_reload_service.SetConfigReloadCallback([this]()
                                                             { ReloadEngineConfig(); });

            _file_change_dispatcher.SetReloadService(&_resource_reload_service);
            _file_change_dispatcher.RegisterExtensionHandler(L".json", [](const FileChangeEvent &event)
                                                             {
                                                                 const fs::path file(event._path);
                                                                 JsonArchive ar;
                                                                 ar.Load(file);
                                                                 if (!ar.IsLoaded())
                                                                     return;
                                                                 const String filename = file.filename().string();
                                                                 if (filename.find("EditorStyle") != String::npos)
                                                                 {
                                                                     auto t = EditorStyle::StaticType();
                                                                     for (auto &p: t->GetProperties())
                                                                         p.Deserialize(&g_editor_style, ar);
                                                                 }
                                                                 else if (filename.find("UITheme_") != String::npos)
                                                                 {
                                                                     auto t = UI::UITheme::StaticType();
                                                                     for (auto &p: t->GetProperties())
                                                                         p.Deserialize(&g_editor_ui_theme, ar);
                                                                     g_editor_ui_theme.PostDeserialize();
                                                                 }
                                                              });
            _file_change_dispatcher.AddPostDispatchHandler([this](const FileChangeEvent &event)
                                                           { _on_file_changed_delegate.Invoke(fs::path(event._path)); });

            _file_watch_service.AddDirectory(ResourceMgr::EngineResRootPath() + EnginePath::kEngineShaderPathW);
            _file_watch_service.AddDirectory(ResourceMgr::EngineResRootPath() + EnginePath::kEngineTexturePathW);
            _file_watch_service.AddDirectory(ResourceMgr::EngineResRootPath() + EnginePath::kEngineScriptPathW);
            _file_watch_service.AddDirectory(ResourceMgr::ProjectRootPath() + L"/Assets");
            _file_watch_service.AddDirectory(s_editor_root_path + L"/Res/UI/");
            _file_watch_service.AddFile(s_editor_root_path + L"/EngineConfig.json");
            _file_watch_service.Snapshot();
            ResourceMgr::SetFileWatchService(&_file_watch_service);

            _is_resource_reload_configured = true;
        }

        void EditorApp::WatchDirectory()
        {
            if (!_is_resource_reload_configured)
                ConfigureResourceReloading();

            for (const auto &event: _file_watch_service.PollChanges())
            {
                LOG_INFO(L"file {} changed", event._path);
                _file_change_dispatcher.Dispatch(event);
            }
        }
    }// namespace Editor
}// namespace Ailu
