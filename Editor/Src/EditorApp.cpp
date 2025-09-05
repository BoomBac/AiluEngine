#include "EditorApp.h"
#include "Common/Undo.h"
#include "Widgets/InputLayer.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/Type.h"
#include "Render/Camera.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/Renderer.h"

#include "Objects/JsonArchive.h"
#include "Framework/Parser/TextParser.h"

#include "UI/Widget.h"
#include "UI/Container.h"
#include "Common/EditorStyle.h"

using namespace Ailu;

namespace Ailu
{
    using namespace Render;
    namespace Editor
    {
        namespace fs = std::filesystem;

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
        int EditorApp::Initialize()
        {
            LogMgr::Init();
            Enum::InitTypeInfo();
            //JsonArchive ar;
            //auto c = MakeRef<UI::Widget>();
            //ar.Load("F:\\AiluBuild\\out\\editor\\bin\\x64\\debug\\widget.json");
            //ar >> *c.get();


            auto work_path = GetWorkingPath();
            work_path = Application::GetUseHomePath();
            LOG_INFO(L"WorkPath: {}", work_path);
            //AlluEngine/
            WString prex_w = work_path + L"OneDrive/AiluEngine/";
            ResourceMgr::ConfigRootPath(prex_w);
            s_editor_root_path = prex_w + L"Editor/";
            s_editor_config_path = prex_w + L"Editor/EditorConfig.json";

            ApplicationDesc desc;
            desc._window_width = 1600;
            desc._window_height = 900;
            desc._gameview_width = 1600;
            desc._gameview_height = 900;
            LoadEditorConfig(desc);
            auto ret = Application::Initialize(desc);
            _p_input_layer = new InputLayer();
            PushLayer(_p_input_layer);
            _pipeline.reset(new CommonRenderPipeline());
            Render::RenderPipeline::Register(_pipeline.get());
            {
                //g_pResourceMgr->Load<Scene>(_opened_scene_path);
                g_pSceneMgr->OpenScene(_opened_scene_path);
            }
            LoadEditorResource();
            //JsonArchive ar;
            //ar.Load(Application::Get().GetUseHomePath() + L"OneDrive/AiluEngine/Editor/Res/UI/EditorStyle.json");
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
                for (auto it = g_pResourceMgr->ResourceBegin<Shader>(); it != g_pResourceMgr->ResourceEnd<Shader>(); it++)
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
            _on_file_changed += [](const fs::path &file) {
                for (auto it = g_pResourceMgr->ResourceBegin<ComputeShader>(); it != g_pResourceMgr->ResourceEnd<ComputeShader>(); it++)
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
            _on_file_changed += [](const fs::path &file) {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                WString cur_asset_path = PathUtils::ExtractAssetPath(cur_path);
                for (auto it = g_pResourceMgr->ResourceBegin<Texture2D>(); it != g_pResourceMgr->ResourceEnd<Texture2D>(); it++)
                {
                    auto tex = ResourceMgr::IterToRefPtr<Texture2D>(it);
                    auto linked_asset = g_pResourceMgr->GetLinkedAsset(tex.get());
                    if (linked_asset && !linked_asset->_external_asset_path.empty())
                    {
                        if (cur_asset_path == linked_asset->_external_asset_path)
                        {
                            LOG_WARNING(L"Texture2d {} has changed,but reload not support yet", linked_asset->_asset_path);
                        }
                    }
                }
            };
            _on_file_changed += [this](const fs::path &file) {
                if (auto pos = file.filename().string().find("EditorStyle"); pos == String::npos)
                    return;
                JsonArchive ar;
                ar.Load(file);
                auto t = EditorStyle::StaticType();
                for (auto& p : t->GetProperties())
                    p.Deserialize(&g_editor_style, ar);
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
            Type* type = EditorConfig::StaticType();
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
            FirstPersonCameraController::s_inst._rotation = _editor_config._controller_rot;
            FirstPersonCameraController::s_inst._base_camera_move_speed = _editor_config._move_speed;
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
            _editor_config._window_size = Vector2UInt(_p_window->GetWidth(),_p_window->GetHeight());
            //_editor_config._viewport_size = Vector2UInt((u32)_p_editor_layer->_p_scene_view->Size().x, (u32)_p_editor_layer->_p_scene_view->Size().y);
            _editor_config._position = Camera::sCurrent->Position();
            _editor_config._rotation = Vector4f(Camera::sCurrent->Rotation().x, Camera::sCurrent->Rotation().y, Camera::sCurrent->Rotation().z, Camera::sCurrent->Rotation().w);
            _editor_config._fov = Camera::sCurrent->FovH();
            _editor_config._aspect = Camera::sCurrent->Aspect();
            _editor_config._near = Camera::sCurrent->Near();
            _editor_config._far = Camera::sCurrent->Far();
            _editor_config._move_speed = FirstPersonCameraController::s_inst._base_camera_move_speed;
            _editor_config._controller_rot = FirstPersonCameraController::s_inst._rotation;
            _editor_config._scene_path = ToChar(g_pResourceMgr->GetAssetPath(g_pSceneMgr->ActiveScene()));

            JsonArchive ar;
            Type *type = EditorConfig::StaticType();
            for (auto &it: type->GetProperties())
                it.Serialize(&_editor_config, ar);
            ar.Load(s_editor_config_path);

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
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"file.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"3d.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"shader.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"image.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"dark/material.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"dark/scene.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"point_light.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"directional_light.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"spot_light.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"area_light.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"camera.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"light_probe.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"dark/anim_clip.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineIconPathW + L"dark/skeleton.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"ibl_brdf_lut.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"T_Default_Material_Grid_N.alasset");
            g_pResourceMgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"T_Default_Material_Grid_M.alasset");
            auto mat_creator = [this](const WString &shader_path, const WString &mat_path, const String &mat_name)->Material*
            {
                auto mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(shader_path), mat_name);
                g_pResourceMgr->RegisterResource(mat_path, mat);
                return mat.get();
            };
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/PointLightBillboard", "PointLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/DirectionalLightBillboard", "DirectionalLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/SpotLightBillboard", "SpotLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/AreaLightBillboard", "AreaLightBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/CameraBillboard", "CameraBillboard");
            mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/LightProbeBillboard", "LightProbeBillboard");
            g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"point_light.alasset");
            g_pResourceMgr->Get<Material>(L"Runtime/Material/DirectionalLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"directional_light.alasset");
            g_pResourceMgr->Get<Material>(L"Runtime/Material/SpotLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"spot_light.alasset");
            g_pResourceMgr->Get<Material>(L"Runtime/Material/AreaLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"area_light.alasset");
            g_pResourceMgr->Get<Material>(L"Runtime/Material/CameraBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"camera.alasset");
            g_pResourceMgr->Get<Material>(L"Runtime/Material/LightProbeBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"light_probe.alasset");
            mat_creator(L"Shaders/hlsl/plane_grid.hlsl", L"Runtime/Material/GridPlane", "GridPlane")->SetFloat("_grid_alpha",1.0f);
            Material::s_checker = g_pResourceMgr->Load<Material>(EnginePath::kEngineMaterialPathW + L"M_Default.alasset");
            WatchDirectory();
        }
        struct ReloadReocrd
        {
            u16 _reload_shader_count;
            u16 _reload_compute_count;
            u16 _reload_tex2d_count;
            bool Empty() const
            {
                return !(_reload_shader_count+_reload_compute_count+_reload_tex2d_count);
            }
        };

        static void ReloadAsset(const fs::path &file, ReloadReocrd& record)
        {
            const WString cur_path = PathUtils::FormatFilePath(file.wstring());
            WString cur_asset_path = PathUtils::ExtractAssetPath(cur_path);
            LOG_INFO("Asset {} changed...", file.string());
            for (auto it = g_pResourceMgr->ResourceBegin<Shader>(); it != g_pResourceMgr->ResourceEnd<Shader>(); it++)
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

            for (auto it = g_pResourceMgr->ResourceBegin<ComputeShader>(); it != g_pResourceMgr->ResourceEnd<ComputeShader>(); it++)
            {
                auto cs = ResourceMgr::IterToRefPtr<ComputeShader>(it);
                if (cs->IsDependencyFile(cur_path))
                {
                    GraphicsContext::Get().CompileShaderAsync(cs.get());
                    ++record._reload_compute_count;
                }
            }

            for (auto it = g_pResourceMgr->ResourceBegin<Texture2D>(); it != g_pResourceMgr->ResourceEnd<Texture2D>(); it++)
            {
                auto tex = ResourceMgr::IterToRefPtr<Texture2D>(it);
                auto linked_asset = g_pResourceMgr->GetLinkedAsset(tex.get());
                if (linked_asset && !linked_asset->_external_asset_path.empty())
                {
                    if (cur_asset_path == linked_asset->_external_asset_path)
                    {
                        LOG_WARNING(L"Texture2d {} has changed,but reload not support yet", linked_asset->_asset_path);
                        // g_pResourceMgr->SubmitTaskSync([=]()->bool
                        // {
                        //    auto handle = ImGuiWidget::DisplayProgressBar(std::format("Reload texture2d: {}...",tex->Name()).c_str(),0.5f);
                        //    TextureImportSetting setting = TextureImportSetting::Default();
                        //    setting._is_reimport = true;
                        //    g_pResourceMgr->Load<Texture2D>(linked_asset->_asset_path,&setting);
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
            static Vector<fs::path> s_watching_paths
            {
                ResourceMgr::EngineResRootPath() + EnginePath::kEngineShaderPathW,
                        ResourceMgr::EngineResRootPath() + EnginePath::kEngineTexturePathW,
                    s_editor_root_path + L"/Res/UI/"
            };
            static bool is_first_execute = true;
            static std::set<fs::path> path_set{};
            static std::unordered_map<fs::path, fs::file_time_type> s_cache_files_time;
            std::unordered_map<fs::path, fs::file_time_type> cur_files_time;
            for(auto& dir : s_watching_paths)
                TraverseDirectory(dir, path_set);
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
                        LOG_INFO("file {} changed", file.string());
                        _on_file_changed_delegate.Invoke(file);
                        s_cache_files_time[file] = cur_files_time[file];
                    }
                }
                //else if (!is_first_execute)
                //    reload_shader(file);
            }
            if (!record.Empty())
            {
                LOG_INFO("Reload {} shader, {} compute shader,{} texture2d", record._reload_shader_count, record._reload_compute_count,record._reload_tex2d_count);
            }
        }
    }// namespace Editor
}// namespace Ailu
