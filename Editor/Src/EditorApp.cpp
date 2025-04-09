#include "EditorApp.h"
#include "Common/Undo.h"
#include "Widgets/InputLayer.h"
#include "Widgets/OutputLog.h"
#include "Widgets/SceneLayer.h"

#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/Type.h"
#include "Render/Camera.h"
#include "Render/CommonRenderPipeline.h"
#include "Render/Renderer.h"

using namespace Ailu;

namespace Ailu
{
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
        int EditorApp::Initialize()
        {
            auto work_path = GetWorkingPath();
            work_path = Application::GetUseHomePath();
            LOG_INFO(L"WorkPath: {}", work_path);
            //AlluEngine/
            WString prex_w = work_path + L"OneDrive/AiluEngine/";
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
            _pipeline.reset(new CommonRenderPipeline());
            g_pGfxContext->RegisterPipeline(_pipeline.get());
            {
                //g_pResourceMgr->Load<Scene>(_opened_scene_path);
                g_pSceneMgr->OpenScene(_opened_scene_path);
            }
            LoadEditorResource();
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
            _p_scene_camera->_anti_aliasing = EAntiAliasing::kTAA;
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
            _p_scene_camera->RecalculateMatrix(true);
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
            auto mat_creator = [this](const WString &shader_path, const WString &mat_path, const String &mat_name)
            {
                g_pResourceMgr->RegisterResource(mat_path, MakeRef<Material>(g_pResourceMgr->Get<Shader>(shader_path), mat_name));
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
            mat_creator(L"Shaders/hlsl/plane_grid.hlsl", L"Runtime/Material/GridPlane", "GridPlane");
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
                                // g_pResourceMgr->SubmitTaskSync([=]()->bool
                                //                                {
                                //     auto handle = ImGuiWidget::DisplayProgressBar(std::format("Reload shader: {}...",shader->Name()).c_str(),0.5f);
                                //     if (shader->PreProcessShader())
                                //     {
                                //         for (auto &mat: shader->GetAllReferencedMaterials())
                                //         {
                                //             mat->ConstructKeywords(shader.get());
                                //             bool is_all_succeed = true;
                                //             is_all_succeed = shader->Compile(); //暂时重编所有变体，避免材质切换变体后使用的是旧的shader
                                //             // for (u16 i = 0; i < shader->PassCount(); i++)
                                //             // {
                                //             //     is_all_succeed &= shader->Compile(i, mat->ActiveVariantHash(i));
                                //             // }
                                //             if (is_all_succeed)
                                //             {
                                //                 mat->ChangeShader(shader.get());
                                //             }
                                //         }
                                //     }
                                //     ImGuiWidget::RemoveProgressBar(handle);
                                //     return true;
                                // });
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
                    // g_pResourceMgr->SubmitTaskSync([=]()->bool
                    //                                {
                    //     auto handle = ImGuiWidget::DisplayProgressBar(std::format("Reload compute shader: {}...",cs->Name()).c_str(),0.5f);
                    //     ComputeShader *shader = cs.get();
                    //     if (shader->Preprocess())
                    //     {
                    //         shader->_is_compiling.store(true);// shader Compile()也会设置这个值，这里设置一下防止读取该值时还没执行compile
                    //         shader->Compile(); 
                    //     }
                    //     ImGuiWidget::RemoveProgressBar(handle);
                    //     return true;
                    // });
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
            static Vector<fs::path> s_watching_paths{
                ResourceMgr::EngineResRootPath() + EnginePath::kEngineShaderPathW,
                ResourceMgr::EngineResRootPath() + EnginePath::kEngineTexturePathW
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
                        ReloadAsset(file,record);
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
