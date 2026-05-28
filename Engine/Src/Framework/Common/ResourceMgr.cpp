#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/AssetDocument.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "GlobalMarco.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "pch.h"

#include "Objects/JsonArchive.h"
#include "Objects/Serialize.h"
#include "Render/GraphicsPipelineStateObject.h"

using namespace Ailu::Render;

namespace Ailu
{
    using namespace SceneManagement;
    namespace
    {
        ResourceMgr *g_pResourceMgr = nullptr;

        WString NormalizeDirectoryPath(const WString &path)
        {
            WString normalized = PathUtils::FormatFilePath(path);
            if (!normalized.empty() && normalized.back() != L'/')
                normalized.push_back(L'/');
            return normalized;
        }

        std::optional<EMeshLoader> ResolveMeshLoader(const WString &path)
        {
            const String ext = StringUtils::ToLower(fs::path(ToChar(path)).extension().string());
            if (ext == ".fbx")
                return EMeshLoader::kFbx;
            if (ext == ".gltf")
                return EMeshLoader::kGltf;
            return std::nullopt;
        }

        void CopyGltfDependencies(const WString &source_gltf_path, const fs::path &copied_gltf_path)
        {
            for (const auto &uri: GltfParser::CollectExternalDependencyUris(source_gltf_path))
            {
                const fs::path source_path = fs::path(source_gltf_path).parent_path() / fs::path(ToWChar(uri));
                const fs::path destination_path = (copied_gltf_path.parent_path() / fs::path(ToWChar(uri))).lexically_normal();
                if (!destination_path.parent_path().empty())
                    FileManager::CreateDirectory(destination_path.parent_path().wstring());
                FileManager::CopyFile(source_path.wstring(), destination_path.wstring());
            }
        }

        bool IsLikelyJsonAssetDocument(const WString &data)
        {
            const WString trimmed = StringUtils::Trim(data);
            return !trimmed.empty() && trimmed.front() == L'{';
        }

        template<typename TDocument>
        bool SaveAssetDocument(const WString &sys_path, TDocument &document)
        {
            Type *type = document.GetType();
            if (type == nullptr)
            {
                LOG_ERROR(L"Save asset document to {} failed, document type is nullptr", sys_path);
                return false;
            }
            JsonArchive ar;
            for (auto &prop: type->GetProperties())
            {
                prop.Serialize(&document, ar);
            }
            ar.Save(sys_path);
            return true;
        }

        template<typename TDocument>
        bool LoadAssetDocument(const WString &sys_path, TDocument &document)
        {
            JsonArchive ar;
            ar.Load(sys_path);
            if (!ar.IsLoaded())
                return false;
            Type *type = document.GetType();
            if (type == nullptr)
            {
                LOG_ERROR(L"Load asset document from {} failed, document type is nullptr", sys_path);
                return false;
            }
            for (auto &prop: type->GetProperties())
            {
                prop.Deserialize(&document, ar);
            }
            return true;
        }

        AssetDocumentHeader MakeAssetDocumentHeader(const Asset *asset)
        {
            AssetDocumentHeader header;
            header._format_version = kSerializedAssetDocumentVersion;
            header._guid = asset->GetGuid().ToString();
            header._asset_type = asset->_asset_type ? asset->_asset_type->FullName() : String{};
            header._asset_name = ToChar(asset->_name);
            return header;
        }

        bool TryLoadAssetDocumentHeader(const WString &sys_path, AssetDocumentHeader &header)
        {
            WString data;
            if (!FileManager::ReadFile(sys_path, data) || !IsLikelyJsonAssetDocument(data))
                return false;

            AssetHeaderProbeDocument probe;
            if (!LoadAssetDocument(sys_path, probe))
                return false;
            if (probe._header._format_version != kSerializedAssetDocumentVersion)
            {
                LOG_ERROR(L"Unsupported asset document version {} in {}", probe._header._format_version, sys_path);
                return false;
            }
            if (probe._header._guid.empty() || probe._header._asset_type.empty())
                return false;
            header = probe._header;
            return true;
        }

        String GetLinkedAssetGuidString(Object *obj)
        {
            if (obj == nullptr)
                return {};

            const Guid &guid = ResourceMgr::Get().GetAssetGuid(obj);
            return guid == Guid::EmptyGuid() ? String{} : guid.ToString();
        }

        void FillMaterialGuidList(const Vector<Ref<Material>> &materials, Vector<String> &out_guids)
        {
            out_guids.clear();
            out_guids.reserve(materials.size());
            for (const auto &material: materials)
            {
                out_guids.emplace_back(GetLinkedAssetGuidString(material.get()));
            }
        }

        void LoadMaterialRefs(const Vector<String> &material_guids, Mesh *mesh, Vector<Ref<Material>> &materials)
        {
            materials.clear();
            materials.reserve(material_guids.size());
            for (u32 index = 0u; index < material_guids.size(); ++index)
            {
                Ref<Material> loaded_material = nullptr;
                const String &material_guid_str = material_guids[index];
                if (!material_guid_str.empty())
                {
                    const Guid material_guid(material_guid_str);
                    ResourceMgr::Get().Load<Material>(material_guid);
                    loaded_material = ResourceMgr::Get().GetRef<Material>(material_guid);
                }
                if (loaded_material == nullptr && mesh != nullptr)
                {
                    loaded_material = ResourceMgr::Get().GetEmbeddedMaterial(mesh, static_cast<u16>(index));
                }
                if (loaded_material != nullptr)
                {
                    materials.emplace_back(loaded_material);
                }
                else
                {
                    LOG_WARNING("Load material slot {} failed for mesh {}", index, mesh != nullptr ? mesh->Name() : String("null"));
                }
            }
        }

        ECS::Entity RemapSceneEntityId(const HashMap<ECS::Entity, ECS::Entity> &old_to_new_entities, u64 legacy_entity_id)
        {
            if (legacy_entity_id == ECS::kInvalidEntity)
                return ECS::kInvalidEntity;

            const auto it = old_to_new_entities.find(static_cast<ECS::Entity>(legacy_entity_id));
            return it != old_to_new_entities.end() ? it->second : ECS::kInvalidEntity;
        }
    }

    void ResourceMgr::Init()
    {
        AL_ASSERT_MSG(g_pResourceMgr == nullptr, "ResourceMgr already init!");
        g_pResourceMgr = new ResourceMgr();
    }

    void ResourceMgr::Shutdown()
    {
        DESTORY_PTR(g_pResourceMgr);
    }

    ResourceMgr& ResourceMgr::Get()
    {
        return *g_pResourceMgr;
    }

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

    WString ResourceMgr::GetAssetTypeName(const Type *type)
    {
        return type ? ToWChar(type->FullName().c_str()) : L"null";
    }

    const Type *ResourceMgr::FindAssetType(const WString &type_name)
    {
        if (type_name.empty())
            return nullptr;
        return Type::Find(ToChar(type_name));
    }
    ResourceMgr::Loader ResourceMgr::GetAssetLoader(const Type *type)
    {
        if (type == Material::StaticType())
            return &ResourceMgr::LoadMaterial;
        if (type == Texture2D::StaticType())
            return &ResourceMgr::LoadTexture;
        if (type == Mesh::StaticType() || type == SkeletonMesh::StaticType())
            return &ResourceMgr::LoadMesh;
        if (type == Shader::StaticType())
            return &ResourceMgr::LoadShader;
        if (type == ComputeShader::StaticType())
            return &ResourceMgr::LoadComputeShader;
        if (type == SceneManagement::Scene::StaticType())
            return &ResourceMgr::LoadScene;
        if (type == AnimationClip::StaticType())
            return &ResourceMgr::LoadAnimClip;
        return nullptr;
    }

    int ResourceMgr::Initialize()
    {
        TimerBlock b("-----------------------------------------------------------ResourceMgr::Initialize");
        AL_ASSERT(!s_engine_res_root_pathw.empty());
        FileManager::SetCurPath(s_engine_res_root_pathw);
        _lut_global_resources_by_type[Material::StaticType()] = {};
        _lut_global_resources_by_type[Texture2D::StaticType()] = {};
        _lut_global_resources_by_type[Texture3D::StaticType()] = {};
        _lut_global_resources_by_type[Mesh::StaticType()] = {};
        _lut_global_resources_by_type[SkeletonMesh::StaticType()] = {};
        _lut_global_resources_by_type[Shader::StaticType()] = {};
        _lut_global_resources_by_type[ComputeShader::StaticType()] = {};
        _lut_global_resources_by_type[Scene::StaticType()] = {};
        _lut_global_resources_by_type[AnimationClip::StaticType()] = {};
        _project_root_path = s_project_root_pathw;
        LoadAssetDB();
        Vector<WString> shader_asset_pathes = {
                L"Shaders/deferred_lighting.alasset",
                L"Shaders/wireframe.alasset",
                L"Shaders/gizmo.alasset",
                L"Shaders/cubemap_gen.alasset",
                L"Shaders/filter_irradiance.alasset",
                L"Shaders/blit.alasset",
                L"Shaders/skybox.alasset",
                L"Shaders/bloom.alasset",
                L"Shaders/forwardlit.alasset",
                L"Shaders/default_ui.alasset",
                L"Shaders/default_text.alasset",
                L"Shaders/voxel_drawer.alasset",
                L"Shaders/texture3d_drawer.alasset",
                L"Shaders/standard_volume.alasset",
                L"Shaders/motion_vector.alasset",
                L"Shaders/terrain.alasset",
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
        //                Core::ThreadPool::Get().Enqueue([&](WString p)
        //                                       { Load<Shader>(p); --shader_load_count ; }, p);
        //
        //            for (auto &p: shader_pathes)
        //                Core::ThreadPool::Get().Enqueue([&](WString p)
        //                                       { RegisterResource(p, LoadExternalShader(p)); --shader_load_count ; }, p);
        //			//RegisterResource(L"Shaders/hlsl/forwardlit.hlsl",LoadExternalShader(L"Shaders/hlsl/forwardlit.hlsl"));
        //
        //			Load<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
        //		}
        JobSystem::Get().Dispatch([](ResourceMgr *mgr)
                               { Shader::s_p_defered_standart_lit = mgr->Load<Shader>(L"Shaders/defered_standard_lit.alasset"); },
                               this);
        Vector<WString> compute_shader_pathes = {
                    L"Shaders/cs_mipmap_gen.alasset",
                    L"Shaders/voxelize.alasset",
                    L"Shaders/ssao_cs.alasset",
                    L"Shaders/taa.alasset",
                    L"Shaders/hzb.alasset",
        };
        for (auto &p: shader_asset_pathes)
            JobSystem::Get().Dispatch([&](WString p)
                                   { Load<Shader>(p); },
                                   p);
        for (auto &p: shader_pathes)
            JobSystem::Get().Dispatch([&](WString p)
                                   { RegisterResource(p, LoadExternalShader(p)); },
                                   p);
        for (auto &p: compute_shader_pathes)
            JobSystem::Get().Dispatch([&](WString p){
                Load<ComputeShader>(p);
            },p);
        JobSystem::Get().Wait();//防止加载mesh时，shader未加载完成
        {
            u8 *default_data = new u8[4 * 4 * 4];
            memset(default_data, 255, 64);
            auto default_white = Texture2D::Create(4, 4,ETextureFormat::kRGBA32);
            default_white->SetPixelData(default_data, 0);
            default_white->Name("default_white");
            default_white->Apply();
            RegisterResource(L"Runtime/default_white", default_white);

            memset(default_data, 0, 64);
            for (int i = 3; i < 64; i += 4)
                default_data[i] = 255;
            auto default_black = Texture2D::Create(4, 4,ETextureFormat::kRGBA32);
            default_black->SetPixelData(default_data, 0);
            default_black->Name("default_black");
            default_black->Apply();
            RegisterResource(L"Runtime/default_black", default_black);
            memset(default_data, 128, 64);
            for (int i = 3; i < 64; i += 4)
                default_data[i] = 255;
            auto default_gray = Texture2D::Create(4, 4,ETextureFormat::kRGBA32);
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
            auto default_normal = Texture2D::Create(4, 4,ETextureFormat::kRGBA32);
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
            TextureImportSetting setting;
            setting._is_sRGB = false;
            setting._generate_mipmap = false;
            auto lut1 = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"ltc_1.dds",setting);
            auto lut2 = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"ltc_2.dds",setting);
            RegisterResource(L"Runtime/ltc_lut1", lut1);
            RegisterResource(L"Runtime/ltc_lut2", lut2);
            auto noise = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"rgba-noise-medium.png",setting);
            RegisterResource(L"Textures/noise_medium.png", noise);
            JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                                   { mgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"blue_noise.alasset",&TextureImportSetting::Default()); },
                                   this);
            JobSystem::Get().Dispatch([this](ResourceMgr *mgr)
                                      { 
                                          auto setting = TextureImportSetting::Default();
                                          setting._is_sRGB = false;
                                          setting._generate_mipmap = false;
                                          auto terrain_map = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"terrain_height.png", setting); 
                                          RegisterResource(L"Textures/TerrainHeight", terrain_map);
                                          setting._is_sRGB = true;
                                          setting._generate_mipmap = true;
                                          terrain_map = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"terrain_color.png", setting); 
                                          RegisterResource(L"Textures/TerrainDiffuse", terrain_map);
                                      },this);
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

        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_cube);Mesh::s_cube = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cube]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                                  {mgr->Load<Mesh>(mesh_path_sphere);Mesh::s_sphere = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_sphere]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_plane);Mesh::s_plane = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_plane]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_capsule);Mesh::s_capsule = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_capsule]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_monkey);Mesh::s_monkey = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_monkey]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_cone);Mesh::s_cone = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cone]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_cylinder);Mesh::s_cylinder = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_cylinder]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                               {mgr->Load<Mesh>(mesh_path_torus);Mesh::s_torus = std::static_pointer_cast<Mesh>(_global_resources[mesh_path_torus]); },
                               this);
        JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
                        {mgr->Load<Mesh>(L"Meshs/terrain_plane.alasset");},
                        this);

        auto FullScreenQuad = MakeRef<Mesh>("FullScreenQuad");
        Vector<u32>  indices = {0, 1, 2, 1, 3, 2};
        FullScreenQuad->SetVertices({{-1.0f, 1.0f, 0.0f},
                                     {1.0f, 1.0f, 0.0f},
                                     {-1.0f, -1.0f, 0.0f},
                                     {1.0f, -1.0f, 0.0f}});
        FullScreenQuad->AddSubmesh(indices);
        FullScreenQuad->SetUVs({{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}}, 0);
        FullScreenQuad->Apply();
        RegisterResource(L"Runtime/Mesh/FullScreenQuad", FullScreenQuad);
        auto FullScreenTriangle = MakeRef<Mesh>("FullScreenTriangle");
        FullScreenTriangle->SetVerticesCount(3);
        FullScreenTriangle->AddSubmesh(Vector<u32>{0, 1, 2});
        FullScreenTriangle->Apply();
        RegisterResource(L"Runtime/Mesh/FullScreenTriangle", FullScreenTriangle);
        Mesh::s_quad = std::static_pointer_cast<Mesh>(_global_resources[L"Runtime/Mesh/FullScreenQuad"]);
        Mesh::s_fullscreen_triangle = std::static_pointer_cast<Mesh>(_global_resources[L"Runtime/Mesh/FullScreenTriangle"]);
        JobSystem::Get().Wait();
        //这里所有需要的shader应该加载完毕，此时再进行预热
        {
            GraphicsPipelineStateMgr::BuildPSOCache();
            Load<Material>(L"Materials/StandardPBR.alasset");
            Material::s_standard_defered_lit = GetRef<Material>(L"Materials/StandardPBR.alasset");
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
            mat_creator(L"Shaders/forwardlit.alasset", L"Runtime/Material/ForwardLit", "ForwardLit");
            mat_creator(L"Shaders/texture3d_drawer.alasset", L"Runtime/Material/Texture3dDrawer", "Texture3dDrawer");
            Material::s_standard_forward_lit = GetRef<Material>(L"Runtime/Material/ForwardLit");
            Material::s_standard_forward_lit.lock()->SetVector("_AlbedoValue",Colors::kWhite);
        }
        //_default_font = Font::Create(GetResSysPath(L"Fonts/Open_Sans/open_sans_regular_65.fnt"));
        _default_font = Font::Create(GetResSysPath(L"Fonts/msdf/Open_Sans/atlas.png"), GetResSysPath(L"Fonts/msdf/Open_Sans/atlas.json"));
        for (auto &p: _default_font->_pages)
        {
            auto setting = TextureImportSetting::Default();
            setting._is_sRGB = false;
            setting._generate_mipmap = false;
            p._texture = LoadExternalTexture(p._file, setting);
            RegisterResource(PathUtils::ExtractAssetPath(p._file), p._texture);
        }
        //Core::ThreadPool::Get().Enqueue("ResourceMgr::WatchDirectory", &ResourceMgr::WatchDirectory, this);
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
            if (asset->_asset_type == Scene::StaticType() || asset->_asset_type == Material::StaticType())
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
        //        LOG_ERROR("Serialize failed when save scene: {}!", clip->Name());
        //        return;
        //    }
        //    WString sys_path = ResourceMgr::GetResSysPath(std::format(L"AnimClips/a.clip"));
        //    if (!FileManager::WriteFile(sys_path, true, ss.str()))
        //    {
        //        LOG_ERROR(L"Save scene failed to {}", sys_path);
        //        return;
        //    }
        //    LOG_INFO(L"Save scene to {}", sys_path);
        //}
    }

    void ResourceMgr::Tick(f32 delta_time)
    {
        while (!_pending_delete_assets.empty())
        {
            Asset *asset = _pending_delete_assets.front();
            auto asset_sys_path = ResourceMgr::GetResSysPath(asset->_asset_path);
            if (_file_last_load_time.contains(asset_sys_path))
            {
                _file_last_load_time.erase(asset_sys_path);
            }
            UnRegisterResource(asset->_asset_path);
            UnRegisterAsset(asset);
            _pending_delete_assets.pop();
        }
        while (!_sync_tasks.empty())
        {
            _sync_tasks.front()();
            _sync_tasks.pop();
        }
        while (!_async_tasks.empty())
        {
            Core::ThreadPool::Get().Enqueue(std::move(_async_tasks.front()));
            _async_tasks.pop();
        }
    }

    void ResourceMgr::SaveAsset(const Asset *asset)
    {
        if (asset->_p_obj == nullptr)
        {
            LOG_WARNING(L"SaveAsset: Asset: {} save failed!it hasn't a instanced object!", asset->_name);
            return;
        }
        AL_ASSERT(!asset->_asset_path.empty());
        if (asset->_asset_type == Mesh::StaticType() || asset->_asset_type == SkeletonMesh::StaticType())
        {
            SaveMesh(asset->_asset_path, asset);
            return;
        }
        if (asset->_asset_type == Shader::StaticType())
        {
            SaveShader(asset->_asset_path, asset);
            return;
        }
        if (asset->_asset_type == ComputeShader::StaticType())
        {
            SaveComputeShader(asset->_asset_path, asset);
            return;
        }
        if (asset->_asset_type == Material::StaticType())
        {
            SaveMaterial(asset->_asset_path, asset->As<Material>());
            return;
        }
        if (asset->_asset_type == Texture2D::StaticType())
        {
            SaveTexture2D(asset->_asset_path, asset);
            return;
        }

        if (asset->_asset_type == Scene::StaticType())
        {
            SaveScene(asset->_asset_path, asset);
            return;
        }
        if (asset->_asset_type == AnimationClip::StaticType())
        {
            SaveAnimClip(asset->_asset_path, asset);
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

    void ResourceMgr::MigrateLegacyAssetDocuments(const WString &root_asset_dir)
    {
        const fs::path root_path = root_asset_dir.empty() ? fs::path(s_engine_res_root_pathw) : fs::path(ResourceMgr::GetResSysPath(root_asset_dir));
        if (!fs::exists(root_path))
        {
            LOG_ERROR(L"MigrateLegacyAssetDocuments: root {} does not exist", root_path.wstring());
            return;
        }

        u32 migrated_count = 0u;
        u32 skipped_count = 0u;
        u32 failed_count = 0u;
        for (const auto &entry: fs::recursive_directory_iterator(root_path))
        {
            if (!entry.is_regular_file())
                continue;

            const String ext = StringUtils::ToLower(entry.path().extension().string());
            if (ext != ".alasset" && ext != ".almap")
                continue;
            if (StringUtils::ToLower(entry.path().filename().string()) == "assetdb.alasset")
                continue;

            const WString sys_path = PathUtils::FormatFilePath(entry.path().wstring());
            WString data;
            if (!FileManager::ReadFile(sys_path, data))
            {
                ++failed_count;
                continue;
            }
            if (IsLikelyJsonAssetDocument(data))
            {
                ++skipped_count;
                continue;
            }

            const WString asset_path = PathUtils::FormatFilePath(fs::relative(entry.path(), fs::path(s_engine_res_root_pathw)).wstring());
            WString asset_name;
            Guid guid;
            const Type *type = nullptr;
            ExtractCommonAssetInfo(asset_path, asset_name, guid, type);
            if (type == nullptr)
            {
                ++skipped_count;
                continue;
            }

            Ref<Object> loaded_asset = nullptr;
            if (type == Shader::StaticType())
                loaded_asset = Load<Shader>(asset_path);
            else if (type == ComputeShader::StaticType())
                loaded_asset = Load<ComputeShader>(asset_path);
            else if (type == Texture2D::StaticType())
                loaded_asset = Load<Texture2D>(asset_path);
            else if (type == Material::StaticType())
                loaded_asset = Load<Material>(asset_path);
            else if (type == Mesh::StaticType() || type == SkeletonMesh::StaticType())
                loaded_asset = Load<Mesh>(asset_path);
            else if (type == Scene::StaticType())
                loaded_asset = Load<Scene>(asset_path);
            else if (type == AnimationClip::StaticType())
                loaded_asset = Load<AnimationClip>(asset_path);
            else
            {
                ++skipped_count;
                continue;
            }

            Asset *asset = GetAsset(asset_path);
            if (!loaded_asset || asset == nullptr)
            {
                LOG_ERROR(L"MigrateLegacyAssetDocuments: failed to reload legacy asset {}", asset_path);
                ++failed_count;
                continue;
            }

            SaveAsset(asset);
            ++migrated_count;
        }

        LOG_INFO(L"Legacy asset migration finished under {}. migrated={}, skipped={}, failed={}", root_path.wstring(), migrated_count, skipped_count, failed_count);
    }

    Asset *ResourceMgr::GetLinkedAsset(Object *obj)
    {
        if (_object_to_asset.contains(obj->ID()))
            return _object_to_asset[obj->ID()];
        return nullptr;
    }


    void ResourceMgr::ConfigProjectRoot(const WString &project_root)
    {
        s_project_root_pathw = NormalizeDirectoryPath(project_root);
        s_engine_res_root_pathw = s_project_root_pathw + L"Engine/Res/";
        kAssetDatabasePath = ToChar(s_engine_res_root_pathw) + "assetdb.alasset";
    }

    void ResourceMgr::ConfigEngineResRoot(const WString &engine_res_root)
    {
        s_engine_res_root_pathw = NormalizeDirectoryPath(engine_res_root);
        fs::path res_path(s_engine_res_root_pathw);
        if (res_path.filename().empty())
            res_path = res_path.parent_path();
        s_project_root_pathw = NormalizeDirectoryPath(res_path.parent_path().parent_path().wstring());
        kAssetDatabasePath = ToChar(s_engine_res_root_pathw) + "assetdb.alasset";
    }

    void ResourceMgr::ConfigRootPath(const WString &prex)
    {
        ConfigProjectRoot(prex);
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
        auto shader = asset->As<Shader>();
        auto [vs, ps] = shader->GetShaderEntry();
        ShaderAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(asset);
        doc._file = ToChar(asset->_external_asset_path);
        doc._vs_entry = vs;
        doc._ps_entry = ps;
        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save shader to {} failed!", sys_path);
        }
    }

    void ResourceMgr::SaveComputeShader(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        ComputeShaderAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(asset);
        doc._file = ToChar(asset->_external_asset_path);
        doc._kernel = "Noname";
        if (auto *setting = dynamic_cast<const ShaderImportSetting *>(_importers[asset_path]); setting != nullptr && !setting->_cs_kernel.empty())
        {
            doc._kernel = setting->_cs_kernel;
        }
        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save compute shader to {} failed!", sys_path);
        }
    }

    Scope<Asset> ResourceMgr::LoadShader(const WString &asset_path,const ImportSetting& settings)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        if (FileManager::ReadFile(sys_path, data))
        {
            if (IsLikelyJsonAssetDocument(data))
            {
                ShaderAssetDocument doc;
                if (!LoadAssetDocument(sys_path, doc))
                    return nullptr;

                auto file = ToWChar(doc._file);
                auto asset = MakeScope<Asset>();
                asset->_asset_path = asset_path;
                asset->_asset_type = Shader::StaticType();
                asset->_external_asset_path = file;
                asset->_p_obj = LoadExternalShader(file);
                return asset;
            }
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
            asset->_asset_type = Shader::StaticType();
            asset->_external_asset_path = file;
            asset->_p_obj = LoadExternalShader(file);
            return asset;
        }
        return nullptr;
    }

    void ResourceMgr::SaveMaterial(const WString &asset_path, Material *mat)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        std::multimap<std::string, ShaderPropertyInfo *> props{};
        auto linked_asset = GetLinkedAsset(mat);
        if (linked_asset == nullptr)
        {
            LOG_ERROR(L"Save material to {} failed, material is not linked to an asset", sys_path);
            return;
        }

        MaterialAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(linked_asset);
        doc._shader_guid = GetAssetGuid(mat->_p_shader).ToString();
        doc._keywords.assign(mat->_all_keywords.begin(), mat->_all_keywords.end());
        for (auto &prop: mat->_properties)
        {
            props.insert(std::make_pair(prop.second._value_name, &prop.second));
        }
        auto float_props = mat->GetAllFloatValue();
        auto vector_props = mat->GetAllVectorValue();
        auto int_vector_props = mat->GetAllIntVectorValue();
        auto uint_props = mat->GetAllUintValue();
        for (auto &[name, value]: uint_props)
        {
            AssetNamedUIntProperty entry;
            entry._name = name;
            entry._value = value;
            doc._uint_properties.push_back(entry);
        }
        for (auto &[name, value]: float_props)
        {
            AssetNamedFloatProperty entry;
            entry._name = name;
            entry._value = value;
            doc._float_properties.push_back(entry);
        }
        for (auto &[name, value]: vector_props)
        {
            AssetNamedVectorProperty entry;
            entry._name = name;
            entry._value = value;
            doc._vector_properties.push_back(entry);
        }
        for (auto &[name, value]: int_vector_props)
        {
            AssetNamedIntVectorProperty entry;
            entry._name = name;
            entry._value = value;
            doc._int_vector_properties.push_back(entry);
        }
        for (auto &[prop_name, prop]: props)
        {
            if (prop->_type == EShaderPropertyType::kTexture2D)
            {
                auto tex = reinterpret_cast<Texture *>(prop->_value_ptr);
                Guid tex_guid;
                if (tex)
                {
                    Asset *lined_asset = GetLinkedAsset(tex);
                    if (lined_asset && lined_asset->_asset_type == Texture2D::StaticType())
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
                AssetTextureBinding entry;
                entry._name = prop->_value_name;
                entry._texture_guid = tex_guid == Guid::EmptyGuid() ? String{} : tex_guid.ToString();
                doc._texture_properties.push_back(entry);
            }
        }
        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save material to {} failed!", sys_path);
            return;
        }
        LOG_WARNING(L"Save material to {}", sys_path);
    }

    void ResourceMgr::SaveMesh(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        MeshAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(asset);
        doc._file = ToChar(asset->_external_asset_path);
        doc._inner_file_name = asset->_p_obj->Name();
        if (auto *setting = dynamic_cast<const MeshImportSetting *>(_importers[asset_path]); setting != nullptr)
        {
            doc._is_combine_mesh = setting->_is_combine_mesh;
        }
        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save mesh to {} failed!", sys_path);
        }
    }

    void ResourceMgr::SaveTexture2D(const WString &asset_path, const Asset *asset)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        Texture2DAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(asset);
        doc._file = ToChar(asset->_external_asset_path);
        if (auto *setting = dynamic_cast<const TextureImportSetting *>(_importers[asset_path]); setting != nullptr)
        {
            doc._is_srgb = setting->_is_sRGB;
        }
        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save texture2d to {} failed!", sys_path);
        }
    }

    void ResourceMgr::SaveScene(const WString &asset_path, const Asset *asset)
    {
        Scene *scene = asset->As<Scene>();
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        SceneAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(asset);

        const ECS::Register &reg = scene->GetRegister();
        const auto &tag_view = reg.View<ECS::TagComponent>();
        doc._entities.reserve(tag_view.size());
        for (u64 index = 0u; index < tag_view.size(); ++index)
        {
            ECS::Entity entity = reg.GetEntity<ECS::TagComponent>(index);
            const ECS::TagComponent &tag = tag_view[index];

            SceneEntityDocument entity_doc;
            entity_doc._entity_id = entity;
            entity_doc._tag_component._name = tag._name;
            entity_doc._tag_component._layer_mask = tag._layer_mask;

            if (const auto *transform = reg.GetComponent<ECS::TransformComponent>(entity); transform != nullptr)
            {
                entity_doc._has_transform_component = true;
                entity_doc._transform_component._position = transform->_transform._position;
                entity_doc._transform_component._rotation = transform->_transform._rotation;
                entity_doc._transform_component._scale = transform->_transform._scale;
            }
            if (const auto *script = reg.GetComponent<ECS::ScriptComponent>(entity); script != nullptr)
            {
                entity_doc._has_script_component = true;
                entity_doc._script_component._script_path = script->_script_path;
            }
            if (const auto *static_mesh = reg.GetComponent<ECS::StaticMeshComponent>(entity); static_mesh != nullptr)
            {
                entity_doc._has_static_mesh_component = true;
                entity_doc._static_mesh_component._mesh_guid = GetLinkedAssetGuidString(static_mesh->_p_mesh.get());
                FillMaterialGuidList(static_mesh->_p_mats, entity_doc._static_mesh_component._material_guids);
            }
            if (const auto *light = reg.GetComponent<ECS::LightComponent>(entity); light != nullptr)
            {
                entity_doc._has_light_component = true;
                entity_doc._light_component._type = ECS::ELightType::ToString(light->_type);
                entity_doc._light_component._light._light_color = light->_light._light_color;
                entity_doc._light_component._light._light_param = light->_light._light_param;
                entity_doc._light_component._light._is_two_side = light->_light._is_two_side;
                entity_doc._light_component._shadow._is_cast_shadow = light->_shadow._is_cast_shadow;
                entity_doc._light_component._shadow._constant_bias = light->_shadow._constant_bias;
                entity_doc._light_component._shadow._slope_bias = light->_shadow._slope_bias;
            }
            if (const auto *hierarchy = reg.GetComponent<ECS::CHierarchy>(entity); hierarchy != nullptr)
            {
                entity_doc._has_hierarchy_component = true;
                entity_doc._hierarchy_component._first_child = hierarchy->_first_child;
                entity_doc._hierarchy_component._prev_sibling = hierarchy->_prev_sibling;
                entity_doc._hierarchy_component._next_sibling = hierarchy->_next_sibling;
                entity_doc._hierarchy_component._parent = hierarchy->_parent;
                entity_doc._hierarchy_component._children_num = hierarchy->_children_num;
                entity_doc._hierarchy_component._inv_matrix_attach = hierarchy->_inv_matrix_attach.ToString();
            }
            if (const auto *camera = reg.GetComponent<ECS::CCamera>(entity); camera != nullptr)
            {
                entity_doc._has_camera_component = true;
                entity_doc._camera_component._type = ECameraType::ToString(camera->_camera.Type());
                entity_doc._camera_component._aspect = camera->_camera.Aspect();
                entity_doc._camera_component._far_clip = camera->_camera.Far();
                entity_doc._camera_component._near_clip = camera->_camera.Near();
                entity_doc._camera_component._fov_h = camera->_camera.FovH();
                entity_doc._camera_component._size = camera->_camera.Size();
            }
            if (const auto *lightprobe = reg.GetComponent<ECS::CLightProbe>(entity); lightprobe != nullptr)
            {
                entity_doc._has_lightprobe_component = true;
                entity_doc._lightprobe_component._size = lightprobe->_size;
                entity_doc._lightprobe_component._is_update_every_tick = lightprobe->_is_update_every_tick;
            }
            if (const auto *rigidbody = reg.GetComponent<ECS::CRigidBody>(entity); rigidbody != nullptr)
            {
                entity_doc._has_rigidbody_component = true;
                entity_doc._rigidbody_component._mass = rigidbody->_mass;
            }
            if (const auto *collider = reg.GetComponent<ECS::CCollider>(entity); collider != nullptr)
            {
                entity_doc._has_collider_component = true;
                entity_doc._collider_component._type = ECS::EColliderType::ToString(collider->_type);
                entity_doc._collider_component._is_trigger = collider->_is_trigger;
                entity_doc._collider_component._center = collider->_center;
                entity_doc._collider_component._param = collider->_param;
            }
            if (const auto *skeleton_mesh = reg.GetComponent<ECS::CSkeletonMesh>(entity); skeleton_mesh != nullptr)
            {
                entity_doc._has_skeleton_mesh_component = true;
                entity_doc._skeleton_mesh_component._mesh_guid = GetLinkedAssetGuidString(skeleton_mesh->_p_mesh.get());
                FillMaterialGuidList(skeleton_mesh->_p_mats, entity_doc._skeleton_mesh_component._material_guids);
                entity_doc._skeleton_mesh_component._anim_clip_guid = GetLinkedAssetGuidString(skeleton_mesh->_anim_clip.get());
            }
            if (const auto *vxgi = reg.GetComponent<ECS::CVXGI>(entity); vxgi != nullptr)
            {
                entity_doc._has_vxgi_component = true;
                entity_doc._vxgi_component._grid_num = vxgi->_grid_num;
                entity_doc._vxgi_component._distance = vxgi->_distance;
            }

            doc._entities.emplace_back(std::move(entity_doc));
        }

        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save scene failed to {}", sys_path);
            return;
        }
        LOG_INFO(L"Save scene to {}", sys_path);
    }
    void ResourceMgr::SaveAnimClip(const WString &asset_path, const Asset *asset)
    {
        AnimationClip *clip = asset->As<AnimationClip>();
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        AnimationClipAssetDocument doc;
        doc._header = MakeAssetDocumentHeader(asset);
        doc._clip_name = clip->Name();
        doc._frame_count = clip->FrameCount();
        doc._duration = clip->Duration();
        doc._frame_rate = clip->FrameRate();
        doc._frame_duration = clip->FrameDuration();
        doc._is_looping = clip->IsLooping();
        doc._tracks.reserve(clip->Size());
        for (u32 index = 0u; index < clip->Size(); ++index)
        {
            const TransformTrack &track = clip->GetTrackAtIndex(index);
            const auto &pos_track = track.GetPositionTrack();
            const auto &rot_track = track.GetRotationTrack();
            const auto &scale_track = track.GetScaleTrack();

            AnimationClipTrackDocument track_doc;
            track_doc._joint_index = clip->GetIdAtIndex(index);
            track_doc._frames.reserve(pos_track.Size());
            for (u32 frame_index = 0u; frame_index < pos_track.Size(); ++frame_index)
            {
                AnimationClipFrameDocument frame_doc;
                frame_doc._position = TrackHelpers::ToVector(pos_track[frame_index]);
                frame_doc._rotation = TrackHelpers::ToQuaternion(rot_track[frame_index]);
                frame_doc._scale = TrackHelpers::ToVector(scale_track[frame_index]);
                track_doc._frames.emplace_back(std::move(frame_doc));
            }
            doc._tracks.emplace_back(std::move(track_doc));
        }

        if (!SaveAssetDocument(sys_path, doc))
        {
            LOG_ERROR(L"Save animclip failed to {}", sys_path);
            return;
        }
        LOG_INFO(L"Save animclip to {}", sys_path);
    }
    //TODO:移除
    std::mutex g_mesh_load_mutex;
    List<Ref<Mesh>> ResourceMgr::LoadExternalMesh(const WString &asset_path, const MeshImportSetting &setting, List<Ref<AnimationClip>> &clips)
    {
        std::unique_lock<std::mutex> lock(g_mesh_load_mutex);
        auto loader = ResolveMeshLoader(asset_path);
        if (!loader.has_value())
        {
            LOG_ERROR(L"Unsupported mesh format: {}", asset_path);
            return {};
        }

        auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(*loader);
        if (parser == nullptr)
        {
            LOG_ERROR(L"Failed to create mesh parser for {}", asset_path);
            return {};
        }

        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        parser->Parser(sys_path, setting);
        List<Ref<Mesh>> mesh_list{};
        parser->GetMeshes(mesh_list);
        for (auto &mesh: mesh_list)
        {
            mesh->Apply();
        }
        for (auto &clip: parser->GetAnimationClips())
            clips.emplace_back(clip);
        return mesh_list;
    }

    Ref<Texture2D> ResourceMgr::LoadExternalTexture(const WString &asset_path,const ImportSetting& settings)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        if (!FileManager::Exist(sys_path))
        {
            LOG_ERROR(L"External texture file {} does not exist!", sys_path);
            return nullptr;
        }
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
        LOG_INFO(L"Start load image file {}...", sys_path);
        auto tex = tex_parser->Parser(sys_path,dynamic_cast<const TextureImportSetting&>(settings));
        tex->Apply();
        return tex;
    }
    bool ResourceMgr::LoadExternalTexture(const WString &asset_path,Ref<Texture2D>& tex,const ImportSetting& settings)
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
        LOG_INFO(L"Start load image file {}...", sys_path);
        bool ret = tex_parser->Parser(sys_path,tex,dynamic_cast<const TextureImportSetting&>(settings));
        tex->Apply();
        return ret;
    }

    Scope<Asset> ResourceMgr::LoadTexture(const WString &asset_path,const ImportSetting& settings)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        auto setting = dynamic_cast<const TextureImportSetting&>(settings);
        if (FileManager::ReadFile(sys_path, data))
        {
            if (IsLikelyJsonAssetDocument(data))
            {
                Texture2DAssetDocument doc;
                if (!LoadAssetDocument(sys_path, doc))
                    return nullptr;
                auto file = ToWChar(doc._file);
                auto json_setting = setting;
                json_setting._is_sRGB = doc._is_srgb;
                if (!IsAssetLoaded(asset_path))
                {
                    auto tex = LoadExternalTexture(file, json_setting);
                    auto asset = MakeScope<Asset>();
                    asset->_asset_path = asset_path;
                    asset->_asset_type = Texture2D::StaticType();
                    asset->_external_asset_path = file;
                    asset->_p_obj = tex;
                    _importers[asset_path] = AL_NEW(TextureImportSetting, json_setting);
                    return asset;
                }

                AL_ASSERT(false);
                auto exist_asset = GetAsset(asset_path);
                auto tex = exist_asset->AsRef<Texture2D>();
                LoadExternalTexture(file, tex, json_setting);
                auto asset = MakeScope<Asset>();
                asset->_asset_path = asset_path;
                asset->_asset_type = Texture2D::StaticType();
                asset->_external_asset_path = file;
                asset->_p_obj = tex;
                return asset;
            }
            auto c = StringUtils::Split(data, L"\n");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            bool is_srgb = StringUtils::ParseUInt32(ToChar(c[4].substr(c[4].find_first_of(L":") + 2))).value_or(1) == 1;
            setting._is_sRGB = is_srgb;
            if (!IsAssetLoaded(asset_path))
            {
                auto tex = LoadExternalTexture(file,setting);
                auto asset = MakeScope<Asset>();
                asset->_asset_path = asset_path;
                asset->_asset_type = Texture2D::StaticType();
                asset->_external_asset_path = file;
                asset->_p_obj = tex;
                _importers[asset_path] = AL_NEW(TextureImportSetting,setting);
                return asset;
            }
            else
            {
                AL_ASSERT(false);
                auto exist_asset = GetAsset(asset_path);
                auto tex = exist_asset->AsRef<Texture2D>();
                LoadExternalTexture(file,tex,setting);
                auto asset = MakeScope<Asset>();
                asset->_asset_path = asset_path;
                asset->_asset_type = Texture2D::StaticType();
                asset->_external_asset_path = file;
                asset->_p_obj = tex;
                return asset;
            }
        }
        return nullptr;
    }

    Scope<Asset> ResourceMgr::LoadMaterial(const WString &asset_path,const ImportSetting& settings)
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

        if (!lines.empty() && IsLikelyJsonAssetDocument(ToWChar(lines.front())))
        {
            MaterialAssetDocument doc;
            if (!LoadAssetDocument(sys_path, doc))
                return nullptr;

            Shader *shader = Load<Shader>(Guid(doc._shader_guid)).get();
            if (shader == nullptr)
            {
                LOG_ERROR(L"Load material with path: {} failed, shader {} is unavailable", sys_path, ToWChar(doc._shader_guid));
                return nullptr;
            }
            bool is_standard_mat = shader->Name() == "defered_standard_lit";
            Ref<Material> mat = is_standard_mat ? std::static_pointer_cast<Material>(MakeRef<StandardMaterial>(doc._header._asset_name)) : MakeRef<Material>(shader, doc._header._asset_name);
            mat->_all_keywords.clear();
            for (auto &kw: doc._keywords)
            {
                if (!kw.empty())
                    mat->_all_keywords.insert(kw);
            }
            mat->Construct(true);
            for (auto &prop: doc._float_properties)
            {
                mat->SetFloat(prop._name, prop._value);
            }
            for (auto &prop: doc._vector_properties)
            {
                mat->SetVector(prop._name, prop._value);
            }
            for (auto &prop: doc._uint_properties)
            {
                mat->SetInt(prop._name, prop._value);
            }
            for (auto &prop: doc._int_vector_properties)
            {
                mat->SetVector(prop._name, prop._value);
            }
            for (auto &prop: doc._texture_properties)
            {
                if (prop._texture_guid.empty())
                    continue;
                auto texture_asset_path = ResourceMgr::Get().GuidToAssetPath(Guid(prop._texture_guid));
                if (!texture_asset_path.empty())
                {
                    mat->SetTexture(prop._name, Load<Texture2D>(texture_asset_path).get());
                }
                else
                {
                    LOG_WARNING("Load material: {}, property {} failed!", mat->_name, prop._name);
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
            asset->_asset_type = Material::StaticType();
            asset->_p_obj = mat;
            return asset;
        }

        AL_ASSERT_MSG(line_count > 3, "material file error");
        String key{}, guid_str{}, type_str{}, name{}, shader_guid{}, keywords;
        FormatLine(lines[0], key, guid_str);
        FormatLine(lines[1], key, type_str);
        FormatLine(lines[2], key, name);
        FormatLine(lines[3], key, shader_guid);
        FormatLine(lines[4], key, keywords);
        Shader *shader = Load<Shader>(Guid(shader_guid)).get();
        if (shader == nullptr)
        {
            LOG_ERROR(L"Load material with path: {} failed, shader {} is unavailable", sys_path, ToWChar(shader_guid));
            return nullptr;
        }
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
                    mat->SetInt(k, u);
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
                auto asset_path = ResourceMgr::Get().GuidToAssetPath(Guid(v));
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
        asset->_asset_type = Material::StaticType();
        asset->_p_obj = mat;
        return asset;
    }

    Scope<Asset> ResourceMgr::LoadMesh(const WString &asset_path,const ImportSetting& settings)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        List<Ref<AnimationClip>> clips;
        if (FileManager::ReadFile(sys_path, data))
        {
            if (IsLikelyJsonAssetDocument(data))
            {
                MeshAssetDocument doc;
                if (!LoadAssetDocument(sys_path, doc))
                    return nullptr;
                auto file = ToWChar(doc._file);
                MeshImportSetting setting;
                setting._import_flag |= MeshImportSetting::kImportFlagMesh;
                setting._is_import_material = false;
                setting._mesh_name = doc._inner_file_name;
                setting._is_combine_mesh = doc._is_combine_mesh;
                auto &&mesh_list = std::move(LoadExternalMesh(file, setting, clips));
                AL_ASSERT(mesh_list.size() != 0);
                bool is_sk_mesh = dynamic_cast<SkeletonMesh *>(mesh_list.front().get()) != nullptr;
                auto asset = MakeScope<Asset>();
                asset->_asset_path = asset_path;
                asset->_asset_type = is_sk_mesh ? SkeletonMesh::StaticType() : Mesh::StaticType();
                asset->_external_asset_path = file;
                asset->_p_obj = mesh_list.front();
                asset->_name = PathUtils::GetFileName(asset_path);
                CreateAndRegisterEmbeddedMaterial(mesh_list.front().get());
                _importers[asset_path] = AL_NEW(MeshImportSetting, setting);
                return asset;
            }
            auto c = StringUtils::Split(data, L"\n");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            String innear_file_name = ToChar(c[4].substr(c[4].find_first_of(L":") + 2));
            MeshImportSetting setting;
            setting._import_flag |= MeshImportSetting::kImportFlagMesh;
            setting._is_import_material = false;
            setting._mesh_name = innear_file_name;
            setting._is_combine_mesh = c.size() > 6 && c[5].substr(c[5].find_first_of(L":") + 2) == WString(L"true") ? true : false;
            //setting._import_flag |= MeshImportSetting::kImportFlagAnimation;
            auto &&mesh_list = std::move(LoadExternalMesh(file, setting, clips));
            AL_ASSERT(mesh_list.size() !=0);
            bool is_sk_mesh = dynamic_cast<SkeletonMesh *>(mesh_list.front().get()) != nullptr;
            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = is_sk_mesh ? SkeletonMesh::StaticType() : Mesh::StaticType();
            asset->_external_asset_path = file;
            asset->_p_obj = mesh_list.front();
            asset->_name = PathUtils::GetFileName(asset_path);
            CreateAndRegisterEmbeddedMaterial(mesh_list.front().get());
            _importers[asset_path] = AL_NEW(MeshImportSetting,setting);
            return asset;
        }
        return nullptr;
    }

    Scope<Asset> ResourceMgr::LoadComputeShader(const WString &asset_path,const ImportSetting& settings)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        if (FileManager::ReadFile(sys_path, data))
        {
            if (IsLikelyJsonAssetDocument(data))
            {
                ComputeShaderAssetDocument doc;
                if (!LoadAssetDocument(sys_path, doc))
                    return nullptr;
                auto file = ToWChar(doc._file);
                auto asset = MakeScope<Asset>();
                asset->_asset_path = asset_path;
                asset->_asset_type = ComputeShader::StaticType();
                asset->_external_asset_path = file;
                asset->_p_obj = LoadExternalComputeShader(file);
                return asset;
            }
            auto c = StringUtils::Split(data, L"\n");
            WString file = c[3].substr(c[3].find_first_of(L":") + 2);
            String kernel = ToChar(c[4].substr(c[4].find_first_of(L":") + 2));
            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = ComputeShader::StaticType();
            asset->_external_asset_path = file;
            asset->_p_obj = LoadExternalComputeShader(file);
            return asset;
        }
        return nullptr;
    }

    Scope<Asset> ResourceMgr::LoadScene(const WString &asset_path,const ImportSetting& settings)
    {
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        if (!FileManager::ReadFile(sys_path, data))
            return nullptr;

        if (IsLikelyJsonAssetDocument(data))
        {
            SceneAssetDocument doc;
            if (!LoadAssetDocument(sys_path, doc))
                return nullptr;

            const String scene_name = !doc._header._asset_name.empty() ? doc._header._asset_name : ToChar(PathUtils::GetFileName(asset_path).c_str());
            Ref<Scene> loaded_scene = MakeRef<Scene>(scene_name);
            auto &reg = loaded_scene->GetRegister();
            HashMap<ECS::Entity, ECS::Entity> old_to_new_entities;
            old_to_new_entities.reserve(doc._entities.size());

            for (const auto &entity_doc: doc._entities)
            {
                ECS::Entity new_entity = reg.Create();
                old_to_new_entities.emplace(static_cast<ECS::Entity>(entity_doc._entity_id), new_entity);

                auto &tag = reg.AddComponent<ECS::TagComponent>(new_entity);
                tag._name = entity_doc._tag_component._name;
                tag._layer_mask = entity_doc._tag_component._layer_mask;
            }

            for (const auto &entity_doc: doc._entities)
            {
                const auto entity_it = old_to_new_entities.find(static_cast<ECS::Entity>(entity_doc._entity_id));
                if (entity_it == old_to_new_entities.end())
                    continue;
                const ECS::Entity entity = entity_it->second;

                if (entity_doc._has_transform_component)
                {
                    auto &component = reg.AddComponent<ECS::TransformComponent>(entity);
                    component._transform._position = entity_doc._transform_component._position;
                    component._transform._rotation = entity_doc._transform_component._rotation;
                    component._transform._scale = entity_doc._transform_component._scale;
                }
                if (entity_doc._has_script_component)
                {
                    auto &component = reg.AddComponent<ECS::ScriptComponent>(entity);
                    component._script_path = entity_doc._script_component._script_path;
                    component.ResetRuntime();
                }
                if (entity_doc._has_static_mesh_component)
                {
                    auto &component = reg.AddComponent<ECS::StaticMeshComponent>(entity);
                    if (!entity_doc._static_mesh_component._mesh_guid.empty())
                    {
                        const Guid mesh_guid(entity_doc._static_mesh_component._mesh_guid);
                        ResourceMgr::Get().Load<Mesh>(mesh_guid);
                        component._p_mesh = ResourceMgr::Get().GetRef<Mesh>(mesh_guid);
                        if (component._p_mesh != nullptr)
                        {
                            component._transformed_aabbs.resize(component._p_mesh->SubmeshCount() + 1u);
                        }
                    }
                    LoadMaterialRefs(entity_doc._static_mesh_component._material_guids, component._p_mesh.get(), component._p_mats);
                }
                if (entity_doc._has_light_component)
                {
                    auto &component = reg.AddComponent<ECS::LightComponent>(entity);
                    if (!entity_doc._light_component._type.empty())
                    {
                        component._type = ECS::ELightType::FromString(entity_doc._light_component._type);
                    }
                    component._light._light_color = entity_doc._light_component._light._light_color;
                    component._light._light_param = entity_doc._light_component._light._light_param;
                    component._light._is_two_side = entity_doc._light_component._light._is_two_side;
                    component._shadow._is_cast_shadow = entity_doc._light_component._shadow._is_cast_shadow;
                    component._shadow._constant_bias = entity_doc._light_component._shadow._constant_bias;
                    component._shadow._slope_bias = entity_doc._light_component._shadow._slope_bias;
                }
                if (entity_doc._has_camera_component)
                {
                    auto &component = reg.AddComponent<ECS::CCamera>(entity);
                    if (!entity_doc._camera_component._type.empty())
                    {
                        component._camera.Type(ECameraType::FromString(entity_doc._camera_component._type));
                    }
                    component._camera.Aspect(entity_doc._camera_component._aspect);
                    component._camera.Far(entity_doc._camera_component._far_clip);
                    component._camera.Near(entity_doc._camera_component._near_clip);
                    component._camera.FovH(entity_doc._camera_component._fov_h);
                    component._camera.Size(entity_doc._camera_component._size);
                    component._camera.MarkDirty();
                    component._camera.RecalculateMatrix(true);
                }
                if (entity_doc._has_lightprobe_component)
                {
                    auto &component = reg.AddComponent<ECS::CLightProbe>(entity);
                    component._size = entity_doc._lightprobe_component._size;
                    component._is_update_every_tick = entity_doc._lightprobe_component._is_update_every_tick;
                    component._is_dirty = true;
                }
                if (entity_doc._has_rigidbody_component)
                {
                    auto &component = reg.AddComponent<ECS::CRigidBody>(entity);
                    component._mass = entity_doc._rigidbody_component._mass;
                }
                if (entity_doc._has_collider_component)
                {
                    auto &component = reg.AddComponent<ECS::CCollider>(entity);
                    if (!entity_doc._collider_component._type.empty())
                    {
                        component._type = ECS::EColliderType::FromString(entity_doc._collider_component._type);
                    }
                    component._is_trigger = entity_doc._collider_component._is_trigger;
                    component._center = entity_doc._collider_component._center;
                    component._param = entity_doc._collider_component._param;
                }
                if (entity_doc._has_skeleton_mesh_component)
                {
                    auto &component = reg.AddComponent<ECS::CSkeletonMesh>(entity);
                    if (!entity_doc._skeleton_mesh_component._mesh_guid.empty())
                    {
                        const Guid mesh_guid(entity_doc._skeleton_mesh_component._mesh_guid);
                        ResourceMgr::Get().Load<SkeletonMesh>(mesh_guid);
                        component._p_mesh = ResourceMgr::Get().GetRef<SkeletonMesh>(mesh_guid);
                        if (component._p_mesh != nullptr)
                        {
                            component._transformed_aabbs.resize(component._p_mesh->SubmeshCount() + 1u);
                        }
                    }
                    LoadMaterialRefs(entity_doc._skeleton_mesh_component._material_guids, component._p_mesh.get(), component._p_mats);
                    if (!entity_doc._skeleton_mesh_component._anim_clip_guid.empty())
                    {
                        const Guid clip_guid(entity_doc._skeleton_mesh_component._anim_clip_guid);
                        ResourceMgr::Get().Load<AnimationClip>(clip_guid);
                        component._anim_clip = ResourceMgr::Get().GetRef<AnimationClip>(clip_guid);
                    }
                }
                if (entity_doc._has_vxgi_component)
                {
                    auto &component = reg.AddComponent<ECS::CVXGI>(entity);
                    component._grid_num = entity_doc._vxgi_component._grid_num;
                    component._distance = entity_doc._vxgi_component._distance;
                }
            }

            for (const auto &entity_doc: doc._entities)
            {
                if (!entity_doc._has_hierarchy_component)
                    continue;

                const auto entity_it = old_to_new_entities.find(static_cast<ECS::Entity>(entity_doc._entity_id));
                if (entity_it == old_to_new_entities.end())
                    continue;
                const ECS::Entity entity = entity_it->second;

                auto &component = reg.AddComponent<ECS::CHierarchy>(entity);
                component._first_child = RemapSceneEntityId(old_to_new_entities, entity_doc._hierarchy_component._first_child);
                component._prev_sibling = RemapSceneEntityId(old_to_new_entities, entity_doc._hierarchy_component._prev_sibling);
                component._next_sibling = RemapSceneEntityId(old_to_new_entities, entity_doc._hierarchy_component._next_sibling);
                component._parent = RemapSceneEntityId(old_to_new_entities, entity_doc._hierarchy_component._parent);
                component._children_num = entity_doc._hierarchy_component._children_num;
                if (!entity_doc._hierarchy_component._inv_matrix_attach.empty())
                {
                    component._inv_matrix_attach.FromString(entity_doc._hierarchy_component._inv_matrix_attach);
                }
                if (auto *transform = reg.GetComponent<ECS::TransformComponent>(entity); transform != nullptr)
                {
                    transform->_transform._p_parent = nullptr;
                    if (component._parent != ECS::kInvalidEntity)
                    {
                        if (auto *parent_transform = reg.GetComponent<ECS::TransformComponent>(component._parent); parent_transform != nullptr)
                        {
                            transform->_transform._p_parent = &parent_transform->_transform;
                        }
                    }
                }
            }

            loaded_scene->MarkDirty();
            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = Scene::StaticType();
            asset->_p_obj = loaded_scene;
            return asset;
        }

        String scene_data = ToChar(data);
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
        asset->_asset_type = Scene::StaticType();
        asset->_p_obj = loaded_scene;
        return asset;
    }
    Scope<Asset> ResourceMgr::LoadAnimClip(const WString &asset_path,const ImportSetting& settings)
    {
        WString sys_path = ResourceMgr::GetResSysPath(asset_path);
        WString data;
        if (!FileManager::ReadFile(sys_path, data))
            return nullptr;

        if (IsLikelyJsonAssetDocument(data))
        {
            AnimationClipAssetDocument doc;
            if (!LoadAssetDocument(sys_path, doc))
                return nullptr;

            Ref<AnimationClip> loaded_clip = MakeRef<AnimationClip>();
            loaded_clip->Name(!doc._clip_name.empty() ? doc._clip_name : doc._header._asset_name);
            loaded_clip->FrameCount(doc._frame_count);
            loaded_clip->Duration(doc._duration);
            loaded_clip->FrameRate(doc._frame_rate);
            const f32 frame_duration = doc._frame_duration > 0.0f ? doc._frame_duration : ((doc._frame_count > 0u && doc._duration > 0.0f) ? (doc._duration / static_cast<f32>(doc._frame_count)) : 0.0f);
            loaded_clip->FrameDuration(frame_duration);
            loaded_clip->IsLooping(doc._is_looping);
            loaded_clip->StartTime(0.0f);
            loaded_clip->EndTime(doc._duration);

            for (const auto &track_doc: doc._tracks)
            {
                auto &track = (*loaded_clip)[track_doc._joint_index];
                track.Resize(track_doc._frames.size());
                f32 cur_time = 0.0f;
                for (u32 frame_index = 0u; frame_index < track_doc._frames.size(); ++frame_index)
                {
                    const auto &frame_doc = track_doc._frames[frame_index];
                    auto pos_frame = TrackHelpers::FromVector(frame_doc._position);
                    pos_frame._time = cur_time;
                    auto rot_frame = TrackHelpers::FromQuaternion(frame_doc._rotation);
                    rot_frame._time = cur_time;
                    auto scale_frame = TrackHelpers::FromVector(frame_doc._scale);
                    scale_frame._time = cur_time;
                    track.GetPositionTrack()[frame_index] = pos_frame;
                    track.GetRotationTrack()[frame_index] = rot_frame;
                    track.GetScaleTrack()[frame_index] = scale_frame;
                    cur_time += frame_duration;
                }
            }
            if (doc._duration <= 0.0f)
            {
                loaded_clip->RecalculateDuration();
            }

            auto asset = MakeScope<Asset>();
            asset->_asset_path = asset_path;
            asset->_asset_type = AnimationClip::StaticType();
            asset->_p_obj = loaded_clip;
            AnimationClipLibrary::AddClip(loaded_clip);
            return asset;
        }

        String clip_data = ToChar(data);
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
        asset->_asset_type = AnimationClip::StaticType();
        asset->_p_obj = loaded_clip;
        AnimationClipLibrary::AddClip(loaded_clip);
        return asset;
    }

    Asset *ResourceMgr::CreateAsset(const WString &asset_path, Ref<Object> obj, bool overwrite)
    {
        bool is_already_exist = _asset_looktable.contains(asset_path);
        if (is_already_exist && !overwrite)
        {
            LOG_WARNING(L"CreateAsset: Asset: {} already exist!", asset_path);
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
        const Type *asset_type = GetObjectResourceType(obj.get());
        new_asset = MakeScope<Asset>(new_guid, asset_type, asset_path);
        if (asset_type == Mesh::StaticType() || asset_type == SkeletonMesh::StaticType())
        {
            new_asset->_addi_info = std::format(L"_{}", ToWChar(obj->Name().c_str()));
        }
        if (asset_type == Shader::StaticType())
        {
            Shader *shader = dynamic_cast<Shader *>(obj.get());
            auto [vs, ps] = shader->GetShaderEntry();
            new_asset->_addi_info = std::format(L"_{}_{}", ToWChar(vs.c_str()), ToWChar(ps.c_str()));
        }
        AL_ASSERT(new_asset != nullptr);
        if (obj)
        {
            new_asset->_p_obj = obj;
            _object_to_asset.emplace(obj->ID(), new_asset.get());
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

    bool ResourceMgr::ExistInAssetDB(const Asset *asset) const
    {
        return _asset_db.contains(asset->GetGuid());
    }

    bool ResourceMgr::ExistInAssetDB(const WString &asset_path) const
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
            LOG_WARNING(L"Rename asset {} whih name {} failed,try another name!", p_asset->_asset_path, new_name);
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
            LOG_WARNING(L"MoveAsset to path {} failed,try another name!", new_asset_path);
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

    bool ResourceMgr::IsAssetLoaded(const WString &asset_path) const
    {
        if (ExistInAssetDB(asset_path))
        {
            return _global_resources.contains(asset_path);
        }
        return false;
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
            const Type *asset_type = FindAssetType(tokens[2]);
            auto asset = MakeScope<Asset>(Guid(guid), asset_type, asset_path);
            asset->Name(ToChar(PathUtils::GetFileName(asset_path).c_str()));
            //先占位，不进行资源加载，实际有使用时才加载。
            RegisterAsset(std::move(asset));
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
                file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << GetAssetTypeName(asset->_asset_type) << std::endl;
            else
                file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << GetAssetTypeName(asset->_asset_type);
            ++cur_count;
        }
        //_asset_db.clear();
    }

    Ref<Material> ResourceMgr::GetEmbeddedMaterial(Mesh *mesh, u16 slot)
    {
        auto id = std::format("EmbeddedMaterial/{}/{}", mesh->Name(), slot);
        if (auto it = _global_resources.find(ToWChar(id)); it != _global_resources.end())
        {
            return std::dynamic_pointer_cast<Material>(it->second);
        }
        return nullptr;
    }

    void ResourceMgr::CreateAndRegisterEmbeddedMaterial(Mesh *mesh)
    {
        if (mesh->GetCacheMaterials().empty())
            return;
        u16 index = 0u;
        for (auto it = mesh->GetCacheMaterials().begin(); it != mesh->GetCacheMaterials().end(); it++)
        {
            auto mat = MakeRef<StandardMaterial>("MAT_" + it->_name + "_embedded");
            if (!it->_textures[0].empty())
            {
                String tex_file_name = PathUtils::GetFileName(it->_textures[0]);
                auto asset_path = ToWChar(std::format("EmbeddedMaterial/{}/{}/{}", mesh->Name(), index, tex_file_name));
                auto tex = GetRef<Texture2D>(asset_path);
                if (!tex)
                {
                    auto setting = TextureImportSetting::Default();
                    tex = LoadExternalTexture(ToWChar(it->_textures[0]), setting);
                    RegisterResource(asset_path, tex);
                }
                mat->SetTexture(StandardMaterial::StandardPropertyName::kAlbedo._tex_name, tex.get());
            }
            if (!it->_textures[1].empty())
            {
                String tex_file_name = PathUtils::GetFileName(it->_textures[1]);
                auto asset_path = ToWChar(std::format("EmbeddedMaterial/{}/{}/{}", mesh->Name(), index, tex_file_name));
                auto tex = GetRef<Texture2D>(asset_path);
                if (!tex)
                {
                    auto setting = TextureImportSetting::Default();
                    setting._is_sRGB = false;
                    tex = LoadExternalTexture(ToWChar(it->_textures[1]), setting);
                    RegisterResource(asset_path, tex);
                }
                mat->SetTexture(StandardMaterial::StandardPropertyName::kNormal._tex_name, tex.get());
            }
            if (!it->_textures[2].empty())
            {
                String tex_file_name = PathUtils::GetFileName(it->_textures[2]);
                auto asset_path = ToWChar(std::format("EmbeddedMaterial/{}/{}/{}", mesh->Name(), index, tex_file_name));
                auto tex = GetRef<Texture2D>(asset_path);
                if (!tex)
                {
                    auto setting = TextureImportSetting::Default();
                    tex = LoadExternalTexture(ToWChar(it->_textures[2]), setting);
                    RegisterResource(asset_path, tex);
                }
                mat->SetTexture(StandardMaterial::StandardPropertyName::kEmission._tex_name, tex.get());
            }
            mat->SetVector(StandardMaterial::StandardPropertyName::kAlbedo._value_name, it->_diffuse);
            mat->SetFloat(StandardMaterial::StandardPropertyName::kRoughness._value_name, it->_roughness);
            mat->SetVector(StandardMaterial::StandardPropertyName::kEmission._value_name, it->_emissive);
            RegisterResource(ToWChar(std::format("EmbeddedMaterial/{}/{}", mesh->Name(), index)), mat);
            ++index;
        }
    }

    Asset *ResourceMgr::RegisterAsset(Scope<Asset> &&asset, bool override)
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);

        auto is_exist = ExistInAssetDB(asset.get());
        if (is_exist && !override)
        {
            LOG_WARNING(L"Asset {} already exist in database,it will be destory...", asset->_asset_path);
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
            LOG_WARNING(L"Asset {} guid conflict, assign a new one!", asset->_asset_path);
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
            auto resource_type = GetObjectResourceType(obj.get());
            AL_ASSERT(resource_type != nullptr);
            auto &v = _lut_global_resources_by_type[resource_type];
            auto it = std::find_if(v.begin(), v.end(), [&](ResourcePoolContainerIter iter) -> bool
                                   { return iter->first == asset_path; });
            if (it != v.end())
                v.erase(it);
            v.push_back(_global_resources.find(asset_path));
        }
        else
        {
            LOG_WARNING("RegisterResource: skip register {}", obj->Name());
        }
    }

    void ResourceMgr::UnRegisterResource(const WString &asset_path)
    {
        std::lock_guard<std::mutex> lock(_asset_db_mutex);
        if (_global_resources.contains(asset_path))
        {
            auto &obj = _global_resources[asset_path];
            u32 ref_count = obj.use_count();
            auto resource_type = GetObjectResourceType(obj.get());
            AL_ASSERT(resource_type != nullptr);
            auto &v = _lut_global_resources_by_type[resource_type];
            v.erase(std::find_if(v.begin(), v.end(), [&](ResourcePoolContainerIter it) -> bool
                                 { return it->second.get() == obj.get(); }));
            _object_to_asset.erase(obj->ID());
            _lut_global_resources.erase(obj->ID());
            _global_resources.erase(asset_path);
            LOG_WARNING(L"UnRegisterResource: {} ref count is {}", asset_path, ref_count - 1);
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

    void ResourceMgr::ExtractCommonAssetInfo(const WString &asset_path, WString &name, Guid &guid, const Type *&type)
    {
        auto sys_path = ResourceMgr::GetResSysPath(asset_path);
        AssetDocumentHeader header;
        if (TryLoadAssetDocumentHeader(sys_path, header))
        {
            guid = Guid(header._guid);
            name = ToWChar(header._asset_name);
            type = FindAssetType(ToWChar(header._asset_type));
            return;
        }

        WString data;
        if (FileManager::ReadFile(sys_path, data))
        {
            auto c = StringUtils::Split(data, L"\n");
            guid = Guid(ToChar(c[0].substr(c[0].find_first_of(L":") + 2)));
            name = c[2].substr(c[2].find_first_of(L":") + 2);
            type = FindAssetType(c[1].substr(c[1].find_first_of(L":") + 2));
        }
    }

    const Type *ResourceMgr::GetObjectResourceType(Object *obj)
    {
        if (obj == nullptr)
            return nullptr;

        for (auto *type = obj->GetType(); type != nullptr; type = type->BaseType())
        {
            if (type == SkeletonMesh::StaticType())
                return SkeletonMesh::StaticType();
            if (type == Mesh::StaticType())
                return Mesh::StaticType();
            if (type == Material::StaticType())
                return Material::StaticType();
            if (type == Texture2D::StaticType())
                return Texture2D::StaticType();
            if (type == Texture3D::StaticType())
                return Texture3D::StaticType();
            if (type == Shader::StaticType())
                return Shader::StaticType();
            if (type == ComputeShader::StaticType())
                return ComputeShader::StaticType();
            if (type == SceneManagement::Scene::StaticType())
                return SceneManagement::Scene::StaticType();
            if (type == AnimationClip::StaticType())
                return AnimationClip::StaticType();
        }
        return nullptr;
    }

    bool ResourceMgr::IsFileOnDiskUpdated(const WString &sys_path)
    {
        fs::path p(sys_path);
        fs::file_time_type last_load_time;
        fs::file_time_type last_write_time = std::filesystem::last_write_time(p);
        bool newer = true;
        if (_file_last_load_time.contains(sys_path))
        {
            last_load_time = _file_last_load_time[sys_path];
            //前者大于后者就表示其表示的时间点晚于后者
            newer = last_write_time > last_load_time;
        }
        return newer;
    }
    void ResourceMgr::MarkFileTimeStamp(const WString &sys_path)
    {
        _file_last_load_time[sys_path] = fs::file_time_type::clock::now();
    }

    Ref<void> ResourceMgr::ImportResource(const WString &sys_path, const WString &target_dir, const ImportSetting &setting)
    {
        return ImportResourceImpl(sys_path, target_dir, & setting);
    }
    void ResourceMgr::SubmitTaskSync(ResourceTask task)
    {
        _sync_tasks.push([=](){
            task();
        });
    }

    void ResourceMgr::SubmitTaskSync(ResourceTask task,std::function<void(bool)> callback)
    {
        _sync_tasks.push([=](){
            callback(task());
        });
    }

    Ref<void> ResourceMgr::ImportResourceAsync(const WString &sys_path, const WString &target_dir,const ImportSetting &setting, OnResourceTaskCompleted callback)
    {
        _async_tasks.push([=]()
                                  {
			callback(ImportResourceImpl(sys_path,target_dir,&setting));
			return nullptr; });
        return nullptr;
    }

    Ref<void> ResourceMgr::ImportResourceImpl(const WString &sys_path, const WString &target_dir, const ImportSetting *setting)
    {
        const ImportSetting *resolved_setting = setting ? setting : &ImportSetting::Default();
        fs::path p(sys_path),dir(target_dir);
        if (!FileManager::Exist(sys_path))
        {
            LOG_ERROR(L"Path {} not exist on the disk!", sys_path);
            return nullptr;
        }
        if (!fs::exists(dir) || !fs::is_directory(dir))
        {
            LOG_ERROR(L"Target dir {} is's a directory or is not exist,please provide a valid path!", target_dir);
            return nullptr;
        }
        auto ext = p.extension().string();
        if (ext.empty() || (!kHDRImageExt.contains(ext) && !kLDRImageExt.contains(ext) && !kMeshExt.contains(ext)))
        {
            LOG_ERROR(L"Path {} is not a supported file!", sys_path);
            return nullptr;
        }
        if (!IsFileOnDiskUpdated(sys_path))
        {
            LOG_WARNING(L"File {} is new,skip load!", sys_path);
            return nullptr;
        }
        auto res_copy_path = dir / p.filename();
        if (resolved_setting->_is_copy)
        {
            FileManager::CopyFile(sys_path, res_copy_path.wstring());
            if (StringUtils::ToLower(ext) == ".gltf")
                CopyGltfDependencies(sys_path, res_copy_path);
        }
        WString external_asset_path = PathUtils::ExtractAssetPath(PathUtils::FormatFilePath(res_copy_path.wstring()));
        TimeMgr time_mgr;
        Ref<void> ret_res = nullptr;
        Queue<std::tuple<WString, Ref<Object>>> loaded_objects;
        time_mgr.Mark();
        WString created_asset_dir = target_dir;
        if (!created_asset_dir.ends_with(L"/"))
            created_asset_dir.append(L"/");
        created_asset_dir = PathUtils::ExtractAssetPath(created_asset_dir);
        if (ExistInAssetDB(external_asset_path))
        {
            LOG_WARNING(L"Asset with path {} already exist in database,skip import!", external_asset_path);
            return nullptr;
        }
        if (auto mesh_loader = ResolveMeshLoader(sys_path); mesh_loader.has_value())
        {
            auto mesh_import_setting = dynamic_cast<const MeshImportSetting *>(resolved_setting);
            mesh_import_setting = mesh_import_setting ? mesh_import_setting : &MeshImportSetting::Default();
            List<Ref<AnimationClip>> clips;
            const WString mesh_source_path = *mesh_loader == EMeshLoader::kGltf ? sys_path : external_asset_path;
            auto mesh_list = std::move(LoadExternalMesh(mesh_source_path, *mesh_import_setting, clips));
            for (auto &mesh: mesh_list)
            {
                WString imported_asset_path = created_asset_dir;
                imported_asset_path.append(std::format(L"{}.alasset", ToWChar(mesh->Name().c_str())));
                loaded_objects.push(std::make_tuple(imported_asset_path, mesh));
                if (mesh_import_setting->_is_import_material)
                {
                    for (auto it = mesh->GetCacheMaterials().begin(); it != mesh->GetCacheMaterials().end(); it++)
                    {
                        auto mat = MakeRef<StandardMaterial>("MAT_" + it->_name);
                        if (!it->_textures[0].empty())
                        {
                            auto albedo = ImportResource(ToWChar(it->_textures[0]), target_dir);
                            if (albedo != nullptr)
                                mat->SetTexture(StandardMaterial::StandardPropertyName::kAlbedo._tex_name, std::static_pointer_cast<Texture>(albedo).get());
                        }
                        if (!it->_textures[1].empty())
                        {
                            auto normal_setting = TextureImportSetting::Default();
                            normal_setting._is_sRGB = false;
                            auto normal = ImportResource(ToWChar(it->_textures[1]), target_dir, normal_setting);
                            if (normal != nullptr)
                                mat->SetTexture(StandardMaterial::StandardPropertyName::kNormal._tex_name, std::static_pointer_cast<Texture>(normal).get());
                        }
                        if (!it->_textures[2].empty())
                        {
                            auto emissive = ImportResource(ToWChar(it->_textures[2]), target_dir);
                            if (emissive != nullptr)
                                mat->SetTexture(StandardMaterial::StandardPropertyName::kEmission._tex_name, std::static_pointer_cast<Texture>(emissive).get());
                        }
                        mat->SetVector(StandardMaterial::StandardPropertyName::kAlbedo._value_name, it->_diffuse);
                        mat->SetFloat(StandardMaterial::StandardPropertyName::kRoughness._value_name, it->_roughness);
                        mat->SetVector(StandardMaterial::StandardPropertyName::kEmission._value_name, it->_emissive);
                        imported_asset_path = created_asset_dir;
                        imported_asset_path.append(std::format(L"{}.alasset", ToWChar(mat->_name.c_str())));
                        loaded_objects.push(std::make_tuple(imported_asset_path, mat));
                    }
                }
                else
                    CreateAndRegisterEmbeddedMaterial(mesh.get());
            }
            for (auto &clip: clips)
            {
                //AnimationClipLibrary::AddClip(clip);
                WString imported_asset_path = created_asset_dir;
                imported_asset_path.append(std::format(L"{}.alasset", ToWChar(clip->Name().c_str())));
                loaded_objects.push(std::make_tuple(imported_asset_path, clip));
            }
        }
        else if (kHDRImageExt.contains(ext) || kLDRImageExt.contains(ext))
        {
            auto tex_import_setting = dynamic_cast<const TextureImportSetting *>(resolved_setting);
            tex_import_setting = tex_import_setting ? tex_import_setting : &TextureImportSetting::Default();
            auto tex = LoadExternalTexture(external_asset_path,*tex_import_setting);
            WString imported_asset_path = created_asset_dir;
            imported_asset_path.append(std::format(L"{}.alasset", ToWChar(tex->Name().c_str())));
            loaded_objects.push(std::make_tuple(imported_asset_path, tex));
        }
        else {}
        while (!loaded_objects.empty())
        {
            auto &[path, obj] = loaded_objects.front();
            auto new_asset = CreateAsset(path, obj);
            new_asset->_external_asset_path = external_asset_path;
            if (obj->GetType() == Mesh::StaticType() || obj->GetType() == SkeletonMesh::StaticType())
            {
                auto mesh_import_setting = dynamic_cast<const MeshImportSetting *>(resolved_setting);
                mesh_import_setting = mesh_import_setting ? mesh_import_setting : &MeshImportSetting::Default();
                _importers[new_asset->_asset_path] = AL_NEW(MeshImportSetting,(*mesh_import_setting));
            }
            else if (obj->GetType() == Texture2D::StaticType() || obj->GetType() == Texture3D::StaticType())
            {
                auto tex_import_setting = dynamic_cast<const TextureImportSetting *>(resolved_setting);
                tex_import_setting = tex_import_setting ? tex_import_setting : &TextureImportSetting::Default();
                _importers[new_asset->_asset_path] = AL_NEW(TextureImportSetting,(*tex_import_setting));
            }
            LOG_INFO(L"Create asset at path {}", path);
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