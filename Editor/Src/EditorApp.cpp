#include "EditorApp.h"
#include "Common/Undo.h"
#include "Widgets/InputLayer.h"
#include "Widgets/RenderView.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Script/ScriptSystem.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/Type.h"
#include "Render/Camera.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/Renderer.h"
#include "Render/RayTracing/RayTracingShader.h"

#include "Framework/Parser/TextParser.h"
#include "Objects/JsonArchive.h"

#include "Common/CameraControllers.h"
#include "Common/EditorStyle.h"
#include "UI/Container.h"
#include "UI/Widget.h"

using namespace Ailu;

namespace Ailu
{
    using namespace Render;
    namespace Editor
    {
        namespace fs = std::filesystem;

        static void ReloadRayTracingShader(const WString &cur_path)
        {
            for (auto *shader : RayTracingShader::GetLiveInstances())
            {
                if (shader != nullptr && shader->IsDependencyFile(cur_path))
                    GraphicsContext::Get().CompileShaderAsync(shader);
            }
        }

        //填充监听路径下的所有文件
        static void TraverseDirectory(const fs::path &directoryPath, std::set<fs::path> &path_set)
        {
            for (const auto &entry: fs::directory_iterator(directoryPath))
            {
                if (fs::is_directory(entry.status()))
                {
                    TraverseDirectory(entry.path(), path_set);
                }
                else if (fs::is_regular_file(entry.status()))
                {
                    path_set.insert(entry.path());
                }
            }
        }

        CommandManager *g_pCommandMgr = new CommandManager;

        EditorStyle g_editor_style = {};

        EditorApp::EditorApp()
        {
        }

        EditorApp::~EditorApp()
        {
        }
        int EditorApp::Initialize()
        {
            LogMgr::Init();
            Enum::InitTypeInfo();
            //JsonArchive ar;
            //auto c = MakeRef<UI::Widget>();
            //ar.Load("F:\\AiluBuild\\out\\editor\\bin\\x64\\debug\\widget.json");
            //ar >> *c.get();


            WString project_root = PathUtils::FormatFilePath(ToWChar(AILU_PROJECT_SOURCE_ROOT));
            if (!project_root.empty() && project_root.back() != L'/')
                project_root.push_back(L'/');
            Application::SetProjectRootPath(project_root);
            Application::SetEngineConfigPath(Application::ResolveProjectPath(L"Editor/EngineConfig.json"));
            ResourceMgr::ConfigProjectRoot(project_root);
            s_editor_root_path = Application::ResolveProjectPath(L"Editor/");
            s_editor_config_path = Application::ResolveProjectPath(L"Editor/EditorConfig.json");
            LOG_INFO(L"ProjectRoot: {}", project_root);

            ApplicationDesc desc;
            desc._window_width = 1600;
            desc._window_height = 900;
            desc._gameview_width = 1600;
            desc._gameview_height = 900;
            _camera_controller = MakeScope<FirstPersonCameraController>();
            LoadEditorConfig(desc);
            auto ret = Application::Initialize(desc);
            _p_input_layer = new InputLayer();
            PushLayer(_p_input_layer);
            _pipeline.reset(new CommonRenderPipeline());
            Render::RenderPipeline::Register(_pipeline.get());
            {
                //ResourceMgr::Get().Load<Scene>(_opened_scene_path);
                SceneManagement::SceneMgr::Get().OpenScene(_opened_scene_path);
            }
            LoadEditorResource();
            ResourceMgr::Get().MigrateLegacyAssetDocuments();
            //JsonArchive ar;
            //ar.Load(Application::ResolveProjectPath(L"Editor/Res/UI/EditorStyle.json"));
            //auto t = EditorStyle::StaticType();
            //auto ps = &g_editor_style;
            //for (auto &p: t->GetProperties())
            //    p.Deserialize(&g_editor_style, ar);
            g_editor_style = DefaultDark();
            _p_editor_layer = new EditorLayer();
            PushLayer(_p_editor_layer);
            _is_playing_mode = false;
            _is_simulate_mode = false;


            _on_file_changed += [](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                WString cur_asset_path = PathUtils::ExtractAssetPath(cur_path);
                for (auto it = ResourceMgr::Get().ResourceBegin<Shader>(); it != ResourceMgr::Get().ResourceEnd<Shader>(); it++)
                {
                    auto shader = ResourceMgr::IterToRefPtr<Shader>(it);
                    if (shader)
                    {
                        bool match_file = false;
                        for (i16 i = 0; i < shader->PassCount(); i++)
                        {
                            if (match_file)
                                break;
                            const auto &pass = shader->GetPassInfo(i);
                            for (auto &head_file: pass._source_files)
                            {
                                if (head_file == cur_path)
                                {
                                    match_file = true;
                                    GraphicsContext::Get().CompileShaderAsync(shader.get());
                                    //++record._reload_shader_count;
                                }
                            }
                        }
                    }
                } };
            _on_file_changed += [](const fs::path &file)
            {
                for (auto it = ResourceMgr::Get().ResourceBegin<ComputeShader>(); it != ResourceMgr::Get().ResourceEnd<ComputeShader>(); it++)
                {
                    const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                    WString cur_asset_path = PathUtils::ExtractAssetPath(cur_path);
                    auto cs = ResourceMgr::IterToRefPtr<ComputeShader>(it);
                    if (cs->IsDependencyFile(cur_path))
                    {
                        GraphicsContext::Get().CompileShaderAsync(cs.get());
                    }
                }
            };
            _on_file_changed += [](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                ReloadRayTracingShader(cur_path);
            };
            _on_file_changed += [](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                WString cur_asset_path = PathUtils::ExtractAssetPath(cur_path);
                for (auto it = ResourceMgr::Get().ResourceBegin<Texture2D>(); it != ResourceMgr::Get().ResourceEnd<Texture2D>(); it++)
                {
                    auto tex = ResourceMgr::IterToRefPtr<Texture2D>(it);
                    auto linked_asset = ResourceMgr::Get().GetLinkedAsset(tex.get());
                    if (linked_asset && !linked_asset->_external_asset_path.empty())
                    {
                        if (cur_asset_path == linked_asset->_external_asset_path)
                        {
                            LOG_WARNING(L"Texture2d {} has changed,but reload not support yet", linked_asset->_asset_path);
                        }
                    }
                }
            };
            _on_file_changed += [this](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                if (cur_path != _engin_config_path)
                    return;
                ReloadEngineConfig();
            };
            _on_file_changed += [this](const fs::path &file)
            {
                if (auto pos = file.filename().string().find("EditorStyle"); pos == String::npos)
                    return;
                JsonArchive ar;
                ar.Load(file);
                auto t = EditorStyle::StaticType();
                for (auto &p: t->GetProperties())
                    p.Deserialize(&g_editor_style, ar);
            };
            _on_file_changed += [](const fs::path &file)
            {
                ScriptSystem::Get().OnScriptFileChanged(file);
            };
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
            LogMgr::Shutdown();
        }
        void EditorApp::Tick(f32 delta_time)
        {
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
            Type *type = EditorConfig::StaticType();
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
            Type *type = EditorConfig::StaticType();
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
            WatchDirectory();
        }
        struct ReloadReocrd
        {
            u16 _reload_shader_count;
            u16 _reload_compute_count;
            u16 _reload_tex2d_count;
            bool Empty() const
            {
                return !(_reload_shader_count + _reload_compute_count + _reload_tex2d_count);
            }
        };

        static void ReloadAsset(const fs::path &file, ReloadReocrd &record)
        {
            const WString cur_path = PathUtils::FormatFilePath(file.wstring());
            WString cur_asset_path = PathUtils::ExtractAssetPath(cur_path);
            LOG_INFO("Asset {} changed...", file.string());
            for (auto it = ResourceMgr::Get().ResourceBegin<Shader>(); it != ResourceMgr::Get().ResourceEnd<Shader>(); it++)
            {
                auto shader = ResourceMgr::IterToRefPtr<Shader>(it);
                if (shader)
                {
                    bool match_file = false;
                    for (i16 i = 0; i < shader->PassCount(); i++)
                    {
                        if (match_file)
                            break;
                        const auto &pass = shader->GetPassInfo(i);
                        for (auto &head_file: pass._source_files)
                        {
                            if (head_file == cur_path)
                            {
                                match_file = true;
                                GraphicsContext::Get().CompileShaderAsync(shader.get());
                                ++record._reload_shader_count;
                            }
                        }
                    }
                }
            }

            for (auto it = ResourceMgr::Get().ResourceBegin<ComputeShader>(); it != ResourceMgr::Get().ResourceEnd<ComputeShader>(); it++)
            {
                auto cs = ResourceMgr::IterToRefPtr<ComputeShader>(it);
                if (cs->IsDependencyFile(cur_path))
                {
                    GraphicsContext::Get().CompileShaderAsync(cs.get());
                    ++record._reload_compute_count;
                }
            }

            ReloadRayTracingShader(cur_path);

            for (auto it = ResourceMgr::Get().ResourceBegin<Texture2D>(); it != ResourceMgr::Get().ResourceEnd<Texture2D>(); it++)
            {
                auto tex = ResourceMgr::IterToRefPtr<Texture2D>(it);
                auto linked_asset = ResourceMgr::Get().GetLinkedAsset(tex.get());
                if (linked_asset && !linked_asset->_external_asset_path.empty())
                {
                    if (cur_asset_path == linked_asset->_external_asset_path)
                    {
                        LOG_WARNING(L"Texture2d {} has changed,but reload not support yet", linked_asset->_asset_path);
                        // ResourceMgr::Get().SubmitTaskSync([=]()->bool
                        // {
                        //    auto handle = ImGuiWidget::DisplayProgressBar(std::format("Reload texture2d: {}...",tex->Name()).c_str(),0.5f);
                        //    TextureImportSetting setting = TextureImportSetting::Default();
                        //    setting._is_reimport = true;
                        //    ResourceMgr::Get().Load<Texture2D>(linked_asset->_asset_path,&setting);
                        //    ImGuiWidget::RemoveProgressBar(handle);
                        //    return true;
                        // });
                        // ++record._reload_tex2d_count;
                        // LOG_INFO(L"Reloaded texture {}", linked_asset->_asset_path);
                    }
                }
            }
        }

        void EditorApp::WatchDirectory()
        {
            namespace fs = std::filesystem;
            static Vector<fs::path> s_watching_paths{
                    ResourceMgr::EngineResRootPath() + EnginePath::kEngineShaderPathW,
                    ResourceMgr::EngineResRootPath() + EnginePath::kEngineTexturePathW,
                    ResourceMgr::EngineResRootPath() + EnginePath::kEngineScriptPathW,
                    s_editor_root_path + L"/Res/UI/"};
            static Vector<fs::path> s_watching_files{
                    s_editor_root_path + L"/EngineConfig.json"};
            static bool is_first_execute = true;
            static std::set<fs::path> path_set{};
            static std::unordered_map<fs::path, fs::file_time_type> s_cache_files_time;
            std::unordered_map<fs::path, fs::file_time_type> cur_files_time;
            for (auto &dir: s_watching_paths)
                TraverseDirectory(dir, path_set);
            for (auto &file: s_watching_files)
            {
                if (fs::exists(file))
                    path_set.insert(file);
            }
            if (is_first_execute)
            {
                for (auto &cur_path: path_set)
                    s_cache_files_time[cur_path] = fs::last_write_time(cur_path);
                is_first_execute = false;
            }
            for (auto &cur_path: path_set)
            {
                if (!s_cache_files_time.contains(cur_path))
                    s_cache_files_time[cur_path] = fs::last_write_time(cur_path);
                cur_files_time[cur_path] = fs::last_write_time(cur_path);
            }
            ReloadReocrd record{};
            for (const auto &[file, last_write_time]: s_cache_files_time)
            {
                if (cur_files_time.contains(file))
                {
                    if (cur_files_time[file] != last_write_time)
                    {
                        //ReloadAsset(file,record);
                        WString formated_path = PathUtils::FormatFilePath(file.wstring());
                        LOG_INFO(L"file {} changed", formated_path);
                        _on_file_changed_delegate.Invoke(fs::path(formated_path));
                        s_cache_files_time[file] = cur_files_time[file];
                    }
                }
                //else if (!is_first_execute)
                //    reload_shader(file);
            }
            if (!record.Empty())
            {
                LOG_INFO("Reload {} shader, {} compute shader,{} texture2d", record._reload_shader_count, record._reload_compute_count, record._reload_tex2d_count);
            }
        }
    }// namespace Editor
}// namespace Ailu
