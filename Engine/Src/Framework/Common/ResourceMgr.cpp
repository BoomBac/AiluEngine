#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "GlobalMarco.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "pch.h"

#include "Objects/Serialize.h"
#include "Render/GraphicsPipelineStateObject.h"


namespace Ailu
{
    String ResourceMgr::GetResSysPath(const String &sub_path)
    {
        String path = sub_path;
        if (PathUtils::IsSystemPath(path))
            return sub_path;
        if (sub_path.starts_with("/"))
            path = path.substr(1);
        else if (sub_path.starts_with("\\"))
            path = path.substr(2);
        return ToChar(s_engine_res_root_pathw) + path;
    }
    WString ResourceMgr::GetResSysPath(const WString &sub_path)
    {
        WString path = sub_path;
        if (PathUtils::IsSystemPath(path))
            return sub_path;
        if (sub_path.starts_with(L"/"))
            path = path.substr(1);
        else if (sub_path.starts_with(L"\\"))
            path = path.substr(2);
        return s_engine_res_root_pathw + path;
    }
    int ResourceMgr::Initialize()
    {
        TimerBlock b("-----------------------------------------------------------ResourceMgr::Initialize");
        AL_ASSERT(!s_engine_res_root_pathw.empty());
        FileManager::SetCurPath(s_engine_res_root_pathw);
        for (int i = 0; i < EAssetType::COUNT; i++)
        {
            _lut_global_resources_by_type[EAssetType::FromString(EAssetType::_Strings[i])] = Vector<ResourcePoolContainer::iterator>();
        }
        _project_root_path = s_engine_res_root_pathw.substr(0, s_engine_res_root_pathw.find_last_of(L"/"));
        LoadAssetDB();
        Vector<WString> shader_asset_pathes = {
                L"Shaders/deferred_lighting.alasset",
                L"Shaders/wireframe.alasset",
                L"Shaders/gizmo.alasset",
                L"Shaders/depth_only.alasset",
                L"Shaders/cubemap_gen.alasset",
                L"Shaders/filter_irradiance.alasset",
                L"Shaders/blit.alasset",
                L"Shaders/skybox.alasset",
                L"Shaders/bloom.alasset",
                L"Shaders/forwardlit.alasset",
                L"Shaders/default_ui.alasset",
                L"Shaders/default_text.alasset",
                L"Shaders/voxel_drawer.alasset",
                L"Shaders/water.alasset"};
        Vector<WString> shader_pathes = {
                L"Shaders/hlsl/debug.hlsl",
                L"Shaders/hlsl/billboard.hlsl",
                L"Shaders/hlsl/ssao.hlsl",
                L"Shaders/hlsl/plane_grid.hlsl",
                L"Shaders/hlsl/cubemap_debug.hlsl",
                L"Shaders/hlsl/taa.hlsl",
                L"Shaders/hlsl/pick_buffer.hlsl",
                L"Shaders/hlsl/select_buffer.hlsl",
                L"Shaders/hlsl/editor_outline.hlsl",
        };

        //        std::atomic<int> shader_load_count = shader_asset_pathes.size() + shader_pathes.size();
        //		{
        //			Shader::s_p_defered_standart_lit = Load<Shader>(L"Shaders/defered_standard_lit.alasset");
        //            for (auto &p: shader_asset_pathes)
        //                g_pThreadTool->Enqueue([&](WString p)
        //                                       { Load<Shader>(p); --shader_load_count ; }, p);
        //
        //            for (auto &p: shader_pathes)
        //                g_pThreadTool->Enqueue([&](WString p)
        //                                       { RegisterResource(p, LoadExternalShader(p)); --shader_load_count ; }, p);
        //			//RegisterResource(L"Shaders/hlsl/forwardlit.hlsl",LoadExternalShader(L"Shaders/hlsl/forwardlit.hlsl"));
        //
        //			Load<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
        //		}
        g_pJobSystem->Dispatch([](ResourceMgr *mgr)
                               { Shader::s_p_defered_standart_lit = mgr->Load<Shader>(L"Shaders/defered_standard_lit.alasset"); },
                               this);
        g_pJobSystem->Dispatch([](ResourceMgr *mgr)
                               { mgr->Load<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset"); },
                               this);
        g_pJobSystem->Dispatch([](ResourceMgr *mgr)
                               { mgr->Load<ComputeShader>(L"Shaders/voxelize.alasset"); },
                               this);
        for (auto &p: shader_asset_pathes)
            g_pJobSystem->Dispatch([&](WString p)
                                   { Load<Shader>(p); },
                                   p);
        for (auto &p: shader_pathes)
            g_pJobSystem->Dispatch([&](WString p)
                                   { RegisterResource(p, LoadExternalShader(p)); },
                                   p);
        {
            u8 *default_data = new u8[4 * 4 * 4];
            memset(default_data, 255, 64);
            auto default_white = Texture2D::Create(4, 4, false);
            default_white->SetPixelData(default_data, 0);
            default_white->Name("default_white");
            default_white->Apply();
            RegisterResource(L"Runtime/default_white", default_white);

            memset(default_data, 0, 64);
            for (int i = 3; i < 64; i += 4)
                default_data[i] = 255;
            auto default_black = Texture2D::Create(4, 4, false);
            default_black->SetPixelData(default_data, 0);
            default_black->Name("default_black");
            default_black->Apply();
            RegisterResource(L"Runtime/default_black", default_black);
            memset(default_data, 128, 64);
            for (int i = 3; i < 64; i += 4)
                default_data[i] = 255;
            auto default_gray = Texture2D::Create(4, 4, false);
            default_gray->SetPixelData(default_data, 0);
            default_gray->Name("default_gray");
            default_gray->Apply();
            RegisterResource(L"Runtime/default_gray", default_gray);
            memset(default_data, 255, 64);
            for (int i = 0; i < 64; i += 4)
            {
                default_data[i] = 128;
                default_data[i + 1] = 128;
            }
            auto default_normal = Texture2D::Create(4, 4, false);
            default_normal->SetPixelData(default_data, 0);
            default_normal->Name("default_normal");
            default_normal->Apply();
            RegisterResource(L"Runtime/default_normal", default_normal);
            DESTORY_PTRARR(default_data);
            Texture::s_p_default_white = default_white.get();
            Texture::s_p_default_black = default_black.get();
            Texture::s_p_default_gray = default_gray.get();
            Texture::s_p_default_normal = default_normal.get();
            //Load<Texture2D>(EnginePath::kEngineTexturePathW + L"small_cave_1k.alasset");
            auto lut1 = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"ltc_1.dds");
            auto lut2 = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"ltc_2.dds");
            RegisterResource(L"Runtime/ltc_lut1", lut1);
            RegisterResource(L"Runtime/ltc_lut2", lut2);
            auto noise = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"rgba-noise-medium.png");
            RegisterResource(L"Textures/noise_medium.png", noise);
        }
        WString mesh_path_cube = L"Meshs/cube.alasset";
        WString mesh_path_sphere = L"Meshs/sphere.alasset";
        WString mesh_path_plane = L"Meshs/plane.alasset";
        WString mesh_path_monkey = L"Meshs/monkey.alasset";
        WString mesh_path_capsule = L"Meshs/capsule.alasset";
        WString mesh_path_cone = L"Meshs/cone.alasset";
        WString mesh_path_cylinder = L"Meshs/cylinder.alasset";
        WString mesh_path_torus = L"Meshs/torus.alasset";

        //		Load<Mesh>(mesh_path_cube);
        //        Load<Mesh>(mesh_path_sphere);
        //        Load<Mesh>(mesh_path_plane);
        //        Load<Mesh>(mesh_path_monkey);
        //        Load<Mesh>(mesh_path_capsule);
        //		Mesh::s_p_cube = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cube]);
        //		Mesh::s_p_shpere = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_sphere]);
        //		Mesh::s_p_plane = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_plane]);
        //		Mesh::s_p_capsule = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_capsule]);

        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_cube);Mesh::s_p_cube = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cube]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_sphere);Mesh::s_p_shpere = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_sphere]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_plane);Mesh::s_p_plane = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_plane]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_capsule);Mesh::s_p_capsule = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_capsule]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_monkey);Mesh::s_p_monkey = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_monkey]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_cone);Mesh::s_p_monkey = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cone]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_cylinder);Mesh::s_p_cylinder = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cylinder]); },
                               this);
        g_pJobSystem->Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_torus);Mesh::s_p_torus = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_torus]); },
                               this);

        auto FullScreenQuad = MakeRef<Mesh>("FullScreenQuad");
        Vector3f *vertices = new Vector3f[4]{{-1.0f, 1.0f, 0.0f},
                                             {1.0f, 1.0f, 0.0f},
                                             {-1.0f, -1.0f, 0.0f},
                                             {1.0f, -1.0f, 0.0f}};
        u32 *indices = new u32[6]{0, 1, 2, 1, 3, 2};
        Vector2f *uv0 = new Vector2f[4]{{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}};
        FullScreenQuad->_vertex_count = 4;
        FullScreenQuad->SetVertices(vertices);
        FullScreenQuad->AddSubmesh(indices, 6);
        FullScreenQuad->SetUVs(uv0, 0);
        FullScreenQuad->BuildRHIResource();
        RegisterResource(L"Runtime/Mesh/FullScreenQuad", FullScreenQuad);
        auto FullScreenTriangle = MakeRef<Mesh>("FullScreenTriangle");
        FullScreenTriangle->_vertex_count = 3;
        FullScreenTriangle->AddSubmesh(new u32[3]{0, 1, 2}, 3);
        FullScreenTriangle->BuildRHIResource();
        RegisterResource(L"Runtime/Mesh/FullScreenTriangle", FullScreenTriangle);
        Mesh::s_p_quad = std::static_pointer_cast<Mesh>(_global_resources[L"Runtime/Mesh/FullScreenQuad"]);
        Mesh::s_p_fullscreen_triangle = std::static_pointer_cast<Mesh>(_global_resources[L"Runtime/Mesh/FullScreenTriangle"]);
        g_pJobSystem->Wait();
        //这里所有需要的shader应该加载完毕，此时再进行预热
        {
            GraphicsPipelineStateMgr::BuildPSOCache();
            Load<Material>(L"Materials/StandardPBR.alasset");
            Material::s_standard_lit = GetRef<Material>(L"Materials/StandardPBR.alasset");
            auto mat_creator = [this](const WString &shader_path, const WString &mat_path, const String &mat_name)
            {
                RegisterResource(mat_path, MakeRef<Material>(Get<Shader>(shader_path), mat_name));
            };
            mat_creator(L"Shaders/wireframe.alasset", L"Runtime/Material/Wireframe", "Wireframe");
            mat_creator(L"Shaders/skybox.alasset", L"Runtime/Material/Skybox", "Skybox");
            mat_creator(L"Shaders/cubemap_gen.alasset", L"Runtime/Material/CubemapGen", "CubemapGen");
            mat_creator(L"Shaders/filter_irradiance.alasset", L"Runtime/Material/EnvmapFilter", "EnvmapFilter");
            mat_creator(L"Shaders/blit.alasset", L"Runtime/Material/Blit", "Blit");
            mat_creator(L"Shaders/gizmo.alasset", L"Runtime/Material/Gizmo", "GizmoDrawer");
        }
        //_default_font = Font::Create(GetResSysPath(L"Fonts/Open_Sans/Arial.fnt"));
        _default_font = Font::Create(GetResSysPath(L"Fonts/Open_Sans/open_sans_regular_65.fnt"));
        for (auto &p: _default_font->_pages)
        {
            p._texture = LoadExternalTexture(p._file);
            RegisterResource(PathUtils::ExtractAssetPath(p._file), p._texture);
        }
        //g_pThreadTool->Enqueue("ResourceMgr::WatchDirectory", &ResourceMgr::WatchDirectory, this);
        //        std::ifstream is(GetResSysPath(L"AnimClips/a.clip"));
        //        TextIArchive ar(&is);
        //        auto clip = MakeRef<AnimationClip>();
        //        clip->Deserialize(ar);
        //        AnimationClipLibrary::AddClip("load_test", clip);
        return 0;
    }

    void ResourceMgr::Finalize()
    {
        _is_watching_directory = false;
        for (auto &[guid, asset]: _asset_db)
        {
            SaveAsset(asset.get());
        }
        SaveAssetDB();
        //for (auto it = AnimationClipLibrary::Begin(); it != AnimationClipLibrary::End(); it++)
        //{
        //    AnimationClip *clip = it->second.get();
        //    using namespace std;
        //    std::ostringstream ss;
        //    TextOArchive ar(&ss);
        //    try
        //    {
        //        clip->Serialize(ar);
        //    }
        //    catch (const std::exception &)
        //    {
        //        g_pLogMgr->LogErrorFormat("Serialize failed when save scene: {}!", clip->Name());
        //        return;
        //    }
        //    WString sys_path = ResourceMgr::GetResSysPath(std::format(L"AnimClips/a.clip"));
        //    if (!FileManager::WriteFile(sys_path, true, ss.str()))
        //    {
        //        g_pLogMgr->LogErrorFormat(L"Save scene failed to {}", sys_path);
        //        return;
        //    }
        //    g_pLogMgr->LogFormat(L"Save scene to {}", sys_path);
        //}
    }

    void ResourceMgr::Tick(f32 delta_time)
    {
        while (!_pending_delete_assets.empty())
        {
            Asset *asset = _pending_delete_assets.front();
            auto asset_sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
            if (s_file_last_load_time.contains(asset_sys_path))
            {
                s_file_last_load_time.erase(asset_sys_path);
            }
            UnRegisterResource(asset->_asset_path);
            UnRegisterAsset(asset);
            _pending_delete_assets.pop();
        }
        while (!_tasks.empty())
        {
            _tasks.front()();
            _tasks.pop();
        }
        SubmitResourceTask();
    }

    void ResourceMgr::SaveAsset(const Asset *asset)
    {
        using std::endl;
        if (asset->_p_obj == nullptr)
        {
            g_pLogMgr->LogWarningFormat(L"SaveAsset: Asset: {} save failed!it hasn't a instanced object!", asset->_name);
            return;
        }
        AL_ASSERT(!asset->_asset_path.empty());
        //写入公共头
        auto sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
        std::wofstream out_asset_file(sys_path, std::ios::out | std::ios::trunc);
        AL_ASSERT(out_asset_file.is_open());
        out_asset_file << "guid: " << ToWStr(asset->GetGuid().ToString()) << endl;
        out_asset_file << "type: " << EAssetType::ToString(asset->_asset_type) << endl;
        out_asset_file << "name: " << asset->_name << endl;
        out_asset_file.close();

        switch (asset->_asset_type)
        {
            case Ailu::EAssetType::kMesh:
            {
                SaveMesh(sys_path, asset);
            }
                return;
            case Ailu::EAssetType::kShader:
            {
                SaveShader(sys_path, asset);
            }
                return;
            case Ailu::EAssetType::kComputeShader:
            {
                SaveComputeShader(sys_path, asset);
            }
                return;
            case Ailu::EAssetType::kMaterial:
            {
                SaveMaterial(sys_path, asset->As<Material>());
            }
                return;
            case Ailu::EAssetType::kTexture2D:
            {
                SaveTexture2D(sys_path, asset);
            }
                return;
            case Ailu::EAssetType::kScene:
            {
                SaveScene(sys_path, asset);
                return;
            }
            case Ailu::EAssetType::kSkeletonMesh:
            {
                SaveMesh(sys_path, asset);
                return;
            }
            case Ailu::EAssetType::kAnimClip:
            {
                SaveAnimClip(sys_path, asset);
                return;
            }
                return;
        }
        AL_ASSERT(false);
    }

    void ResourceMgr::SaveAllUnsavedAssets()
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);
        while (!s_pending_save_assets.empty())
        {
            SaveAsset(s_pending_save_assets.front());
            s_pending_save_assets.pop();
        }
        SaveAssetDB();
    }

    Asset *ResourceMgr::GetLinkedAsset(Object *obj)
    {
        if (_object_to_asset.contains(obj->ID()))
            return _object_to_asset[obj->ID()];
        return nullptr;
    }


    void ResourceMgr::ConfigRootPath(const WString &prex)
    {
        s_engine_res_root_pathw = prex + L"Engine/Res/";
        kAssetDatabasePath = ToChar(s_engine_res_root_pathw) + "assetdb.alasset";
    }
    Ref<Shader> ResourceMgr::LoadExternalShader(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        //auto s_setting = dynamic_cast<const ShaderImportSetting *>(setting);
        auto s = Shader::Create(sys_path);
        return s;
    }

    Ref<ComputeShader> ResourceMgr::LoadExternalComputeShader(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        //auto s_setting = dynamic_cast<const ShaderImportSetting *>(setting);
        return ComputeShader::Create(sys_path);
    }

    void ResourceMgr::SaveShader(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        if (FileManager::Exist(sys_path))
        {
            auto shader = asset->As<Shader>();
            auto [vs, ps] = shader->GetShaderEntry();
            std::wstringstream wss;
            WString indent = L"  ";
            wss << indent << L"file: " << asset->_external_asset_path << std::endl;
            wss << indent << L"vs_entry: " << ToWStr(vs.c_str()) << std::endl;
            wss << indent << L"ps_entry: " << ToWStr(ps.c_str()) << std::endl;
            if (FileManager::WriteFile(sys_path, true, wss.str()))
            {
                return;
            }
        }
        g_pLogMgr->LogErrorFormat(L"Save shader to {} failed!", sys_path);
    }

    void ResourceMgr::SaveComputeShader(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        if (FileManager::Exist(sys_path))
        {
            auto cs = asset->As<ComputeShader>();
            //String kernel = cs->KernelName();
            std::wstringstream wss;
            WString indent = L"  ";
            wss << indent << L"file: " << asset->_external_asset_path << std::endl;
            //wss << indent << L"kernel: " << ToWStr(kernel.c_str()) << std::endl;
            wss << indent << L"kernel: " << L"Noname" << std::endl;
            if (FileManager::WriteFile(sys_path, true, wss.str()))
            {
                return;
            }
        }
        g_pLogMgr->LogErrorFormat(L"Save compute shader to {} failed!", sys_path);
    }

    Scope<Asset> ResourceMgr::LoadShader(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        Ref<Shader> shader;
        if (FileManager::ReadFile(sys_path, data))
        {
            auto c = StringUtils::Split(data, L"\n");
            AL_ASSERT_MSG(c.size() > 4, "Invalid shader asset file format!");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            WString vs_entry = c[4].substr(c[4].find_first_of(L":") + 2);
            WString ps_entry = c[5].substr(c[5].find_first_of(L":") + 2);
            ShaderImportSetting setting;
            setting._vs_entry = ToChar(vs_entry);
            setting._ps_entry = ToChar(ps_entry);
            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = EAssetType::kShader;
            asset->_external_asset_path = file;
            asset->_p_obj = LoadExternalShader(file);
            return asset;
        }
        return nullptr;
    }

    void ResourceMgr::SaveMaterial(const WString &asset_path, Material *mat)
    {
        using std::endl;
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        std::multimap<std::string, ShaderPropertyInfo *> props{};
        //为了写入通用资产头信息，暂时使用追加方式打开
        std::ofstream out_mat(sys_path, std::ios::out | std::ios::app);
        out_mat << "shader_guid: " << GetAssetGuid(mat->_p_shader).ToString() << endl;
        out_mat << "keywords: " << su::Join(mat->_all_keywords, ",") << endl;
        for (auto &prop: mat->_properties)
        {
            props.insert(std::make_pair(prop.second._value_name, &prop.second));
        }
        //out_mat << endl;
        auto float_props = mat->GetAllFloatValue();
        auto vector_props = mat->GetAllVectorValue();
        auto int_vector_props = mat->GetAllIntVectorValue();
        auto uint_props = mat->GetAllUintValue();
        //auto tex_props = mat->GetAllTexture();
        out_mat << "  prop_type: "
                << ShaderPropertyType::Uint << endl;
        for (auto &[name, value]: uint_props)
        {
            out_mat << "    " << name << ": " << value << endl;
        }
        out_mat << "  prop_type: "
                << ShaderPropertyType::Float << endl;
        for (auto &[name, value]: float_props)
        {
            out_mat << "    " << name << ": " << value << endl;
        }
        out_mat << "  prop_type: "
                << ShaderPropertyType::Vector << endl;
        for (auto &[name, value]: vector_props)
        {
            out_mat << "    " << name << ": " << value << endl;
        }
        out_mat << "  prop_type: "
                << ShaderPropertyType::IntVector << endl;
        for (auto &[name, value]: int_vector_props)
        {
            out_mat << "    " << name << ": " << value << endl;
        }
        out_mat << "  prop_type: "
                << "Texture2D" << endl;
        for (auto &[prop_name, prop]: props)
        {
            if (prop->_type == EShaderPropertyType::kTexture2D)
            {
                auto tex = reinterpret_cast<Texture *>(prop->_value_ptr);
                Guid tex_guid;
                if (tex)
                {
                    Asset *lined_asset = GetLinkedAsset(tex);
                    if (lined_asset && lined_asset->_asset_type == EAssetType::kTexture2D)
                        tex_guid = lined_asset->GetGuid();
                    else
                    {
                        AL_ASSERT(true);
                        LOG_ERROR("Texture2D {} hasn't a linked asset or asset type error!", tex->Name());
                        tex_guid = Guid::EmptyGuid();
                    }
                }
                else
                    tex_guid = Guid::EmptyGuid();
                out_mat << "    " << prop->_value_name << ": " << tex_guid.ToString() << endl;
            }
        }
        out_mat.close();
        LOG_WARNING(L"Save material to {}", sys_path);
    }

    void ResourceMgr::SaveMesh(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        if (FileManager::Exist(sys_path))
        {
            std::wstringstream wss;
            wss << L"file: " << asset->_external_asset_path << std::endl;
            wss << L"inner_file_name: " << ToWStr(asset->_p_obj->Name().c_str()) << std::endl;
            if (FileManager::WriteFile(sys_path, true, wss.str()))
            {
                return;
            }
        }
        g_pLogMgr->LogErrorFormat(L"Save mesh to {} failed!", sys_path);
    }

    void ResourceMgr::SaveTexture2D(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        if (FileManager::Exist(sys_path))
        {
            std::wstringstream wss;
            WString indent = L"  ";
            wss << indent << L"file: " << asset->_external_asset_path << std::endl;
            if (FileManager::WriteFile(sys_path, true, wss.str()))
            {
                return;
            }
        }
        g_pLogMgr->LogErrorFormat(L"Save texture2d to {} failed!", sys_path);
    }

    void ResourceMgr::SaveScene(const WString &asset_path, const Asset *asset)
    {
        Scene *scene = asset->As<Scene>();
        using namespace std;
        std::ostringstream ss;
        TextOArchive ar(&ss);
        try
        {
            scene->Serialize(ar);
        }
        catch (const std::exception &e)
        {
            g_pLogMgr->LogErrorFormat("Serialize failed when save scene: {} with exce {}", scene->Name(), e.what());
            return;
        }
        WString sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
        if (!FileManager::WriteFile(sys_path, true, ss.str()))
        {
            g_pLogMgr->LogErrorFormat(L"Save scene failed to {}", sys_path);
            return;
        }
        g_pLogMgr->LogFormat(L"Save scene to {}", sys_path);
    }
    void ResourceMgr::SaveAnimClip(const WString &asset_path, const Asset *asset)
    {
        AnimationClip *clip = asset->As<AnimationClip>();
        using namespace std;
        std::ostringstream ss;
        TextOArchive ar(&ss);
        try
        {
            clip->Serialize(ar);
        }
        catch (const std::exception &)
        {
            g_pLogMgr->LogErrorFormat("Serialize failed when save animclip: {}!", clip->Name());
            return;
        }
        WString sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
        if (!FileManager::WriteFile(sys_path, true, ss.str()))
        {
            g_pLogMgr->LogErrorFormat(L"Save animclip failed to {}", sys_path);
            return;
        }
        g_pLogMgr->LogFormat(L"Save animclip to {}", sys_path);
    }

    List<Ref<Mesh>> ResourceMgr::LoadExternalMesh(const WString &asset_path, const MeshImportSetting &setting, List<Ref<AnimationClip>> &clips)
    {
        static auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        parser->Parser(sys_path, setting);
        List<Ref<Mesh>> mesh_list{};
        parser->GetMeshes(mesh_list);
        auto &imported_mat_infos = parser->GetImportedMaterialInfos();
        for (auto &mesh: mesh_list)
        {
            for (auto it = imported_mat_infos.begin(); it != imported_mat_infos.end(); ++it)
            {
                mesh->AddCacheMaterial(*it);
            }
            g_pGfxContext->SubmitRHIResourceBuildTask([=]()
                                                      { mesh->BuildRHIResource(); });
        }
        for (auto &clip: parser->GetAnimationClips())
            clips.emplace_back(clip);
        return mesh_list;
    }

    Ref<Texture2D> ResourceMgr::LoadExternalTexture(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        String ext = fs::path(ToChar(asset_path.c_str())).extension().string();
        Scope<ITextureParser> tex_parser = nullptr;
        if (kLDRImageExt.contains(ext))
        {
            tex_parser = std::move(TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG));
        }
        else if (kHDRImageExt.contains(ext))
        {
            tex_parser = std::move(TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kHDR));
        }
        else if (ext == ".DDS" || ext == ".dds")
        {
            tex_parser = std::move(TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kDDS));
        }
        else {};
        AL_ASSERT(tex_parser != nullptr);
        g_pLogMgr->LogFormat(L"Start load image file {}...", sys_path);
        auto tex = tex_parser->Parser(sys_path);
        g_pGfxContext->SubmitRHIResourceBuildTask([=]()
                                                  { tex->Apply(); });
        return tex;
    }

    Scope<Asset> ResourceMgr::LoadTexture(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        Ref<Texture2D> shader;
        if (FileManager::ReadFile(sys_path, data))
        {
            auto c = StringUtils::Split(data, L"\n");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            auto tex = LoadExternalTexture(file);
            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = EAssetType::kTexture2D;
            asset->_external_asset_path = file;
            asset->_p_obj = tex;
            return asset;
        }
        return nullptr;
    }

    Scope<Asset> ResourceMgr::LoadMaterial(const WString &asset_path)
    {
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        std::ifstream file(sys_path);
        if (!file.is_open())
        {
            LOG_ERROR(L"Load material with path: {} failed!", sys_path);
            return nullptr;
        }
        std::vector<String> lines{};
        std::string line;
        int line_count = 0u;
        while (std::getline(file, line))
        {
            lines.emplace_back(line);
            ++line_count;
        }
        file.close();
        AL_ASSERT_MSG(line_count > 3, "material file error");
        String key{}, guid_str{}, type_str{}, name{}, shader_guid{}, keywords;
        FormatLine(lines[0], key, guid_str);
        FormatLine(lines[1], key, type_str);
        FormatLine(lines[2], key, name);
        FormatLine(lines[3], key, shader_guid);
        FormatLine(lines[4], key, keywords);
        Shader *shader = Get<Shader>(Guid(shader_guid));
        bool is_standard_mat = shader->Name() == "defered_standard_lit";
        Ref<Material> mat = nullptr;
        if (is_standard_mat)
        {
            mat = MakeRef<StandardMaterial>(name);
        }
        else
        {
            mat = MakeRef<Material>(shader, name);
        }
        mat->_all_keywords.clear();
        for (auto &kw: su::Split(keywords, ","))
        {
            mat->_all_keywords.insert(kw);
        }
        mat->Construct(true);
        std::string cur_type{" "};
        std::string prop_type{"prop_type"};
        u32 prop_begin_line = 5;
        for (u32 i = prop_begin_line; i < lines.size(); ++i)
        {
            String k{}, v{};
            FormatLine(lines[i], k, v);
            if (k == prop_type && cur_type != v)
            {
                cur_type = v;
                continue;
            }
            if (cur_type == ShaderPropertyType::Float)
            {
                float f;
                if (sscanf_s(v.c_str(), "%f", &f) == 1)
                    mat->SetFloat(k, f);
                else
                    LOG_WARNING("Load material: {},property {} failed!", mat->_name, k);
            }
            else if (cur_type == ShaderPropertyType::Vector)
            {
                Vector4f vec{};
                if (sscanf_s(v.c_str(), "%f,%f,%f,%f", &vec.r, &vec.g, &vec.b, &vec.a) == 4)
                {
                    mat->SetVector(k, vec);
                    //LOG_INFO("Set {} value {},get value {}",k,vec.ToString(),mat->GetVector(k).ToString());
                }
                else
                    LOG_WARNING("Load material: {},property {} failed!", mat->_name, k);
            }
            else if (cur_type == ShaderPropertyType::Uint)
            {
                uint32_t u;
                if (sscanf_s(v.c_str(), "%u", &u) == 1)
                    mat->SetUint(k, u);
                else
                    LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
            }
            else if (cur_type == ShaderPropertyType::IntVector)
            {
                Vector4Int vec{};
                if (sscanf_s(v.c_str(), "%d, %d, %d, %d", &vec.r, &vec.g, &vec.b, &vec.a) == 4)
                {
                    mat->SetVector(k, vec);
                }
                else
                    LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
            }
            else if (cur_type == ShaderPropertyType::Texture2D)
            {
                if (v.empty() || v == "null guid") continue;
                auto asset_path = g_pResourceMgr->GuidToAssetPath(Guid(v));
                if (!asset_path.empty())
                {
                    mat->SetTexture(k, Load<Texture2D>(asset_path).get());
                }
                else
                {
                    LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
                }
            }
        }
        mat->GetUint("_MaterialID");
        if (is_standard_mat)
        {
            auto standard_mat = static_cast<StandardMaterial *>(mat.get());

            standard_mat->SurfaceType((ESurfaceType::ESurfaceType) standard_mat->GetUint("_surface"));
            standard_mat->MaterialID((EMaterialID::EMaterialID) standard_mat->GetUint("_MaterialID"));
        }
        auto asset = MakeScope<Asset>();
        asset->_asset_path = asset_path;
        asset->_asset_type = EAssetType::kMaterial;
        asset->_p_obj = mat;
        return asset;
    }

    Scope<Asset> ResourceMgr::LoadMesh(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        List<Ref<AnimationClip>> clips;
        if (FileManager::ReadFile(sys_path, data))
        {
            auto c = StringUtils::Split(data, L"\n");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            String innear_file_name = ToChar(c[4].substr(c[4].find_first_of(L":") + 2));
            MeshImportSetting setting;
            setting._import_flag |= MeshImportSetting::kImportFlagMesh;
            setting._is_import_material = false;
            //setting._import_flag |= MeshImportSetting::kImportFlagAnimation;
            auto &&mesh_list = std::move(LoadExternalMesh(file, setting, clips));
            for (auto it = mesh_list.begin(); it != mesh_list.end(); ++it)
            {
                if (it->get()->Name() == innear_file_name)
                {
                    bool is_sk_mesh = dynamic_cast<SkeletonMesh *>(it->get()) != nullptr;
                    auto asset = MakeScope<Asset>();
                    asset->_asset_path = asset_path;
                    asset->_asset_type = is_sk_mesh ? EAssetType::kSkeletonMesh : EAssetType::kMesh;
                    asset->_external_asset_path = file;
                    asset->_p_obj = *it;
                    asset->_name = PathUtils::GetFileName(asset_path);
                    return asset;
                }
                else
                {
                    LOG_WARNING("Mesh innear_file_name conflict between {} and {} when load {}!", it->get()->Name(), innear_file_name, ToChar(asset_path));
                }
            }
        }
        return nullptr;
    }

    Scope<Asset> ResourceMgr::LoadComputeShader(const WString &asset_path)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        Ref<ComputeShader> cs;
        if (FileManager::ReadFile(sys_path, data))
        {
            auto c = StringUtils::Split(data, L"\n");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            String kernel = ToChar(c[4].substr(c[4].find_first_of(L":") + 2));
            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = EAssetType::kComputeShader;
            asset->_external_asset_path = file;
            asset->_p_obj = LoadExternalComputeShader(file);
            return asset;
        }
        return nullptr;
    }

    Scope<Asset> ResourceMgr::LoadScene(const WString &asset_path)
    {
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        String scene_data;
        FileManager::ReadFile(sys_path, scene_data);
        su::RemoveSpaces(scene_data);
        std::stringstream ss(scene_data);
        String line;
        std::getline(ss, line);
        Guid guid(su::Split(line, ":")[1]);
        std::getline(ss, line);
        std::getline(ss, line);
        line = su::Split(line, ":")[1];
        //这里到达场景根节点
        TextIArchive arch(&ss);
        Ref<Scene> loaded_scene = MakeRef<Scene>(line);
        loaded_scene->Deserialize(arch);
        auto asset = MakeScope<Asset>();
        asset->_asset_path = asset_path;
        asset->_asset_type = EAssetType::kScene;
        asset->_p_obj = loaded_scene;
        return asset;
    }
    Scope<Asset> ResourceMgr::LoadAnimClip(const WString &asset_path)
    {
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        String clip_data;
        FileManager::ReadFile(sys_path, clip_data);
        su::RemoveSpaces(clip_data);
        std::stringstream ss(clip_data);
        String line;
        std::getline(ss, line);
        Guid guid(su::Split(line, ":")[1]);
        std::getline(ss, line);
        std::getline(ss, line);
        line = su::Split(line, ":")[1];
        //这里到达clip根节点
        TextIArchive arch(&ss);
        Ref<AnimationClip> loaded_clip = MakeRef<AnimationClip>();
        loaded_clip->Deserialize(arch);
        auto asset = MakeScope<Asset>();
        asset->_asset_path = asset_path;
        asset->_asset_type = EAssetType::kAnimClip;
        asset->_p_obj = loaded_clip;
        AnimationClipLibrary::AddClip(loaded_clip);
        return asset;
    }

    Asset *ResourceMgr::CreateAsset(const WString &asset_path, Ref<Object> obj, bool overwrite)
    {
        bool is_already_exist = _asset_looktable.contains(asset_path);
        if (is_already_exist && !overwrite)
        {
            g_pLogMgr->LogWarningFormat(L"CreateAsset: Asset: {} already exist!", asset_path);
        }
        Guid new_guid = Guid::Generate();
        Scope<Asset> new_asset = nullptr;
        {
            std::lock_guard<std::mutex> l(_asset_db_mutex);
            while (_asset_db.contains(new_guid))
            {
                new_guid = Guid::Generate();
            }
        }
        EAssetType::EAssetType asset_type = GetObjectAssetType(obj.get());
        new_asset = MakeScope<Asset>(new_guid, asset_type, asset_path);
        if (asset_type == EAssetType::kMesh || asset_type == EAssetType::kSkeletonMesh)
        {
            //new_asset = MakeScope<Asset>(new_guid, EAssetType::kMesh, asset_path);
            new_asset->_addi_info = std::format(L"_{}", ToWStr(obj->Name().c_str()));
        }
        if (asset_type == EAssetType::kShader)
        {
            Shader *shader = dynamic_cast<Shader *>(obj.get());
            auto [vs, ps] = shader->GetShaderEntry();
            //new_asset = MakeScope<Asset>(new_guid, EAssetType::kShader, asset_path);
            new_asset->_addi_info = std::format(L"_{}_{}", ToWStr(vs.c_str()), ToWStr(ps.c_str()));
        }
        AL_ASSERT(new_asset != nullptr);
        if (obj)
        {
            new_asset->_p_obj = obj;
            _object_to_asset.emplace(obj->ID(), new_asset.get());
            obj->_guid = new_asset->GetGuid();
        }
        RegisterResource(asset_path, obj);
        s_pending_save_assets.push(RegisterAsset(std::move(new_asset)));
        return s_pending_save_assets.back();
    }

    void ResourceMgr::DeleteAsset(Asset *asset)
    {
        if (ExistInAssetDB(asset))
            _pending_delete_assets.push(asset);
    }

    bool ResourceMgr::ExistInAssetDB(const Asset *asset)
    {
        return _asset_db.contains(asset->GetGuid());
    }

    bool ResourceMgr::ExistInAssetDB(const WString &asset_path)
    {
        return _asset_looktable.contains(asset_path);
    }

    const WString &ResourceMgr::GetAssetPath(Object *obj) const
    {
        if (_object_to_asset.contains(obj->ID()))
        {
            return _object_to_asset.at(obj->ID())->_asset_path;
        }
        return EmptyWString;
    }

    const Guid &ResourceMgr::GetAssetGuid(Object *obj) const
    {
        if (_object_to_asset.contains(obj->ID()))
        {
            return _object_to_asset.at(obj->ID())->GetGuid();
        }
        return Guid::EmptyGuid();
    }

    const WString &ResourceMgr::GuidToAssetPath(const Guid &guid) const
    {
        if (_asset_db.contains(guid))
        {
            return _asset_db.at(guid)->_asset_path;
        }
        return EmptyWString;
    }

    Asset *ResourceMgr::GetAsset(const WString &asset_path) const
    {
        if (_asset_looktable.contains(asset_path))
        {
            return _asset_db.contains(_asset_looktable.at(asset_path)) ? _asset_db.at(_asset_looktable.at(asset_path)).get() : nullptr;
        }
        return nullptr;
    }

    Vector<Asset *> ResourceMgr::GetAssets(const ISearchFilter &filter) const
    {
        Vector<Asset *> assets;
        for (auto &[guid, asset]: _asset_db)
        {
            if (filter.Filter(asset.get()))
                assets.emplace_back(asset.get());
        }
        return assets;
    }

    bool ResourceMgr::RenameAsset(Asset *p_asset, const WString &new_name)
    {
        WString old_asset_path = p_asset->_asset_path;
        WString new_asset_path = PathUtils::RenameFile(p_asset->_asset_path, new_name);
        WString ext_name = PathUtils::ExtractExt(p_asset->_asset_path);
        if (ExistInAssetDB(new_asset_path))
        {
            g_pLogMgr->LogWarningFormat(L"Rename asset {} whih name {} failed,try another name!", p_asset->_asset_path, new_name);
            return false;
        }
        UnRegisterResource(old_asset_path);
        RegisterResource(new_asset_path, p_asset->_p_obj);
        Scope<Asset> asset = std::move(_asset_db.at(p_asset->GetGuid()));
        UnRegisterAsset(p_asset);
        p_asset->_name = new_name;
        p_asset->_asset_path = new_asset_path;
        RegisterAsset(std::move(asset));
        //p_asset->_p_obj->Name(ToChar(new_name));
        return true;
    }

    bool ResourceMgr::MoveAsset(Asset *p_asset, const WString &new_asset_path)
    {
        WString old_asset_path = p_asset->_asset_path;
        if (ExistInAssetDB(new_asset_path))
        {
            g_pLogMgr->LogWarningFormat(L"MoveAsset to path {} failed,try another name!", new_asset_path);
            return false;
        }
        UnRegisterResource(old_asset_path);
        RegisterResource(new_asset_path, p_asset->_p_obj);
        Scope<Asset> asset = std::move(_asset_db.at(p_asset->GetGuid()));
        UnRegisterAsset(p_asset);
        p_asset->_asset_path = new_asset_path;
        RegisterAsset(std::move(asset));
        return true;
    }

    void ResourceMgr::RemoveFromAssetDB(const Asset *asset)
    {
        if (ExistInAssetDB(asset))
        {
            _asset_db.erase(asset->GetGuid());
            _asset_looktable.erase(asset->_asset_path);
        }
    }

    void ResourceMgr::LoadAssetDB()
    {
        std::wifstream file(kAssetDatabasePath);
        if (!file.is_open())
        {
            LOG_ERROR("Load asset_db with path: {} failed!", kAssetDatabasePath);
            return;
        }
        WString line;
        while (std::getline(file, line))
        {
            std::vector<WString> tokens;
            std::wistringstream lineStream(line);
            WString token;
            while (std::getline(lineStream, token, L','))
                tokens.push_back(token);
            String guid = ToChar(tokens[0]);
            WString asset_path = tokens[1];
            EAssetType::EAssetType asset_type = EAssetType::FromString(ToChar(tokens[2]));
            //			if (asset_type == EAssetType::kTexture2D)
            //			{
            //				Load<Texture2D>(asset_path);
            //			}
            //			else
            {
                //先占位，不进行资源加载，实际有使用时才加载。
                RegisterAsset(MakeScope<Asset>(Guid(guid), asset_type, asset_path));
            }
        }
        file.close();
    }

    void ResourceMgr::SaveAssetDB()
    {
        std::wofstream file(kAssetDatabasePath, std::ios::out | std::ios::trunc);
        u64 db_size = _asset_db.size() - 1, cur_count = 0;
        for (auto &[guid, asset]: _asset_db)
        {
            if (cur_count != db_size)
                file << ToWStr(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type) << std::endl;
            else
                file << ToWStr(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type);
            ++cur_count;
        }
        //_asset_db.clear();
    }

    Asset *ResourceMgr::RegisterAsset(Scope<Asset> &&asset, bool override)
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);

        auto is_exist = ExistInAssetDB(asset.get());
        if (is_exist && !override)
        {
            g_pLogMgr->LogWarningFormat(L"Asset {} already exist in database,it will be destory...", asset->_asset_path);
            return nullptr;
        }
        if (is_exist && _asset_db[asset->GetGuid()]->_asset_path != asset->_asset_path)
        {
            Guid new_guid = Guid::Generate();
            while (_asset_db.contains(new_guid))
            {
                new_guid = Guid::Generate();
            }
            is_exist = false;
            g_pLogMgr->LogWarningFormat(L"Asset {} guid conflict, assign a new one!", asset->_asset_path);
            asset->AssignGuid(new_guid);
        }
        if (asset->GetGuid() == Guid::EmptyGuid())
        {
            Guid new_guid = Guid::Generate();
            while (_asset_db.contains(new_guid))
            {
                new_guid = Guid::Generate();
            }
            asset->AssignGuid(new_guid);
        }
        auto guid = asset->GetGuid();
        auto asset_path = asset->_asset_path;
        //延迟加载的资产
        if (is_exist && _asset_db[guid]->_p_obj == nullptr)
        {
            _asset_db[guid]->CopyFrom(*asset.get());
        }
        else
        {
            _asset_db[guid].swap(asset);
        }

        auto cache_asset = _asset_db[guid].get();
        if (cache_asset->_p_obj)
        {
            _object_to_asset[cache_asset->_p_obj->ID()] = cache_asset;
            cache_asset->_p_obj->_guid = cache_asset->GetGuid();
        }
        _asset_looktable[asset_path] = guid;
        return cache_asset;
    }

    void ResourceMgr::UnRegisterAsset(Asset *asset)
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);
        if (ExistInAssetDB(asset->_asset_path))
        {
            if (asset->_p_obj)
            {
                asset->_p_obj->_guid = Guid::EmptyGuid();
                _object_to_asset.erase(asset->_p_obj->ID());
            }
            _asset_looktable.erase(asset->_asset_path);
            _asset_db.erase(asset->GetGuid());
        }
    }

    void ResourceMgr::RegisterResource(const WString &asset_path, Ref<Object> obj, bool override)
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);
        bool exist = _global_resources.contains(asset_path);
        if (exist && override || !exist)
        {
            _global_resources[asset_path] = obj;
            _lut_global_resources[obj->ID()] = _global_resources.find(asset_path);
            auto &v = _lut_global_resources_by_type[GetObjectAssetType(obj.get())];
            auto it = std::find_if(v.begin(), v.end(), [&](ResourcePoolContainerIter iter) -> bool
                                   { return iter->first == asset_path; });
            if (it != v.end())
                v.erase(it);
            v.push_back(_global_resources.find(asset_path));
        }
        else
        {
            g_pLogMgr->LogWarningFormat("RegisterResource: skip register {}", obj->Name());
        }
    }

    void ResourceMgr::UnRegisterResource(const WString &asset_path)
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);
        if (_global_resources.contains(asset_path))
        {
            auto &obj = _global_resources[asset_path];
            u32 ref_count = obj.use_count();
            auto &v = _lut_global_resources_by_type[GetObjectAssetType(obj.get())];
            v.erase(std::find_if(v.begin(), v.end(), [&](ResourcePoolContainerIter it) -> bool
                                 { return it->second.get() == obj.get(); }));
            _lut_global_resources.erase(obj->ID());
            _global_resources.erase(asset_path);
            g_pLogMgr->LogWarningFormat(L"UnRegisterResource: {} ref count is {}", asset_path, ref_count - 1);
        }
    }

    void ResourceMgr::FormatLine(const String &line, String &key, String &value)
    {
        std::istringstream iss(line);
        if (std::getline(iss, key, ':') && std::getline(iss, value))
        {
            key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch)
                                                { return !std::isspace(ch); }));
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](int ch)
                                                    { return !std::isspace(ch); }));
        }
    }

    void ResourceMgr::ExtractCommonAssetInfo(const WString &asset_path, WString &name, Guid &guid, EAssetType::EAssetType &type)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        if (FileManager::ReadFile(sys_path, data))
        {
            auto c = StringUtils::Split(data, L"\n");
            guid = Guid(ToChar(c[0].substr(c[0].find_first_of(L":") + 2)));
            name = c[2].substr(c[2].find_first_of(L":") + 2);
            type = EAssetType::FromString(ToChar(c[1].substr(c[1].find_first_of(L":") + 2)));
        }
    }

    EAssetType::EAssetType ResourceMgr::GetObjectAssetType(Object *obj)
    {
        if (dynamic_cast<Material *>(obj))
        {
            return EAssetType::kMaterial;
        }
        else if (dynamic_cast<Texture2D *>(obj))
        {
            return EAssetType::kTexture2D;
        }
        else if (dynamic_cast<Mesh *>(obj))
        {
            if (dynamic_cast<SkeletonMesh *>(obj))
                return EAssetType::kSkeletonMesh;
            return EAssetType::kMesh;
        }
        else if (dynamic_cast<Shader *>(obj))
        {
            return EAssetType::kShader;
        }
        else if (dynamic_cast<ComputeShader *>(obj))
        {
            return EAssetType::kComputeShader;
        }
        else if (dynamic_cast<AnimationClip *>(obj))
            return EAssetType::kAnimClip;
        return EAssetType::kUndefined;
    }

    bool ResourceMgr::IsFileOnDiskUpdated(const WString &sys_path)
    {
        fs::path p(sys_path);
        fs::file_time_type last_load_time;
        fs::file_time_type last_write_time = std::filesystem::last_write_time(p);
        bool newer = true;
        if (s_file_last_load_time.contains(sys_path))
        {
            last_load_time = s_file_last_load_time[sys_path];
            //前者大于后者就表示其表示的时间点晚于后者
            newer = last_write_time > last_load_time;
        }
        return newer;
    }
    void ResourceMgr::MarkFileTimeStamp(const WString &sys_path)
    {
        s_file_last_load_time[sys_path] = fs::file_time_type::clock::now();
    }

    Ref<void> ResourceMgr::ImportResource(const WString &sys_path, const ImportSetting &setting)
    {
        return ImportResourceImpl(sys_path, &setting);
    }
    void ResourceMgr::SubmitTaskSync(ResourceTask task)
    {
        _tasks.push(task);
    }

    Ref<void> ResourceMgr::ImportResourceAsync(const WString &sys_path, const ImportSetting &setting, OnResourceTaskCompleted callback)
    {
        s_task_queue.emplace_back([=]()
                                  {
			callback(ImportResourceImpl(sys_path, &setting));
			return nullptr; });
        return nullptr;
    }

    void ResourceMgr::AddResourceTask(ResourceTask task)
    {
        s_task_queue.emplace_back(task);
    }

    void ResourceMgr::SubmitResourceTask()
    {
        for (auto &task: s_task_queue)
        {
            g_pThreadTool->Enqueue(std::move(task));
        }
        s_task_queue.clear();
    }

    Ref<void> ResourceMgr::ImportResourceImpl(const WString &sys_path, const ImportSetting *setting)
    {
        fs::path p(sys_path);
        if (!FileManager::Exist(sys_path))
        {
            g_pLogMgr->LogErrorFormat(L"Path {} not exist on the disk!", sys_path);
            return nullptr;
        }
        auto ext = p.extension().string();
        if (ext.empty() || (!kHDRImageExt.contains(ext) && !kLDRImageExt.contains(ext) && !kMeshExt.contains(ext)))
        {
            g_pLogMgr->LogErrorFormat(L"Path {} is not a supported file!", sys_path);
            return nullptr;
        }
        if (!IsFileOnDiskUpdated(sys_path))
        {
            g_pLogMgr->LogWarningFormat(L"File {} is new,skip load!", sys_path);
            return nullptr;
        }
        auto new_sys_path = FileManager::GetCurSysDirStr();
        new_sys_path.append(L"/");
        if (setting->_is_copy)
        {
            new_sys_path += p.filename().wstring();
            if (sys_path.find_first_of(L".") != sys_path.npos)
                FileManager::CopyFile(sys_path, new_sys_path);
            else
            {
                g_pLogMgr->LogErrorFormat(std::source_location::current(), L"Path {} is not a file!", sys_path);
            }
        }
        else
        {
            new_sys_path = sys_path;
        }
        if (!FileManager::Exist(new_sys_path))
        {
            return nullptr;
        }
        PathUtils::FormatFilePathInPlace(new_sys_path);
        WString external_asset_path = PathUtils::ExtractAssetPath(new_sys_path);
        TimeMgr time_mgr;
        Ref<void> ret_res = nullptr;
        Queue<std::tuple<WString, Ref<Object>>> loaded_objects;
        EAssetType::EAssetType asset_type = EAssetType::kUndefined;
        time_mgr.Mark();
        WString created_asset_dir = FileManager::GetCurSysDirStr();
        created_asset_dir.append(L"/");
        created_asset_dir = PathUtils::ExtractAssetPath(created_asset_dir);
        if (ext == ".fbx" || ext == ".FBX")
        {
            asset_type = EAssetType::kMesh;
            auto mesh_import_setting = dynamic_cast<const MeshImportSetting *>(setting);
            mesh_import_setting = mesh_import_setting ? mesh_import_setting : &MeshImportSetting::Default();
            List<Ref<AnimationClip>> clips;
            auto mesh_list = std::move(LoadExternalMesh(external_asset_path, *mesh_import_setting, clips));
            for (auto &mesh: mesh_list)
            {
                WString imported_asset_path = created_asset_dir;
                imported_asset_path.append(std::format(L"{}.alasset", ToWStr(mesh->Name().c_str())));
                loaded_objects.push(std::make_tuple(imported_asset_path, mesh));
                if (mesh_import_setting->_is_import_material)
                {
                    for (auto it = mesh->GetCacheMaterials().begin(); it != mesh->GetCacheMaterials().end(); it++)
                    {
                        auto mat = MakeRef<StandardMaterial>("MAT_" + it->_name);
                        if (!it->_textures[0].empty())
                        {
                            auto albedo = ImportResource(ToWStr(it->_textures[0]));
                            if (albedo != nullptr)
                                mat->SetTexture(StandardMaterial::StandardPropertyName::kAlbedo._tex_name, std::static_pointer_cast<Texture>(albedo).get());
                        }
                        if (!it->_textures[1].empty())
                        {
                            auto normal = ImportResource(ToWStr(it->_textures[1]));
                            if (normal != nullptr)
                                mat->SetTexture(StandardMaterial::StandardPropertyName::kNormal._tex_name, std::static_pointer_cast<Texture>(normal).get());
                        }
                        imported_asset_path = created_asset_dir;
                        imported_asset_path.append(std::format(L"{}.alasset", ToWStr(mat->_name.c_str())));
                        loaded_objects.push(std::make_tuple(imported_asset_path, mat));
                    }
                }
            }
            for (auto &clip: clips)
            {
                //AnimationClipLibrary::AddClip(clip);
                WString imported_asset_path = created_asset_dir;
                imported_asset_path.append(std::format(L"{}.alasset", ToWStr(clip->Name().c_str())));
                loaded_objects.push(std::make_tuple(imported_asset_path, clip));
            }
        }
        else if (kHDRImageExt.contains(ext) || kLDRImageExt.contains(ext))
        {
            asset_type = EAssetType::kTexture2D;
            auto tex = LoadExternalTexture(external_asset_path);
            WString imported_asset_path = created_asset_dir;
            imported_asset_path.append(std::format(L"{}.alasset", ToWStr(tex->Name().c_str())));
            loaded_objects.push(std::make_tuple(imported_asset_path, tex));
        }
        else {}
        while (!loaded_objects.empty())
        {
            auto &[path, obj] = loaded_objects.front();
            CreateAsset(path, obj)->_external_asset_path = external_asset_path;
            g_pLogMgr->LogFormat(L"Create asset at path {}", path);
            loaded_objects.pop();
        }
        OnAssetDataBaseChanged();
        SaveAllUnsavedAssets();
        return ret_res;
    }

    void ResourceMgr::OnAssetDataBaseChanged()
    {
        for (auto &f: _asset_changed_callbacks)
        {
            f();
        }
    }
}// namespace Ailu