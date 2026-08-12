#include "Framework/Common/ResourceMgr.h"
#include "Assets/AssetDocument.h"
#include "Assets/ScriptAsset.h"
#include "Audio/AudioClip.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/JobSystem.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/List.h"
#include "Framework/Core/Containers/Queue.h"
#include "Framework/Common/Assert.h"
#include "Graph/GraphAsset.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "pch.h"

#include "Objects/JsonArchive.h"
#include "Objects/Serialize.h"
#include "Project/ProjectManager.h"
#include "Input/InputActionAsset.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/2D/Sprite.h"
#include "Assets/AssetHandlers.h"

using namespace Ailu::Render;

namespace Ailu
{
	using namespace SceneManagement;
	namespace
	{
		ResourceMgr *g_pResourceMgr = nullptr;
		const WString kEmptyWString;

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
			const Type *type = document.GetType();
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

		WString ResolveExternalAssetPath(const WString &asset_path, const WString &stored_external_path)
		{
			if (stored_external_path.empty() || PathUtils::IsSystemPath(stored_external_path))
				return stored_external_path;

			WString normalized_stored_path = PathUtils::FormatFilePath(stored_external_path);
			const bool has_directory = normalized_stored_path.find(L'/') != WString::npos;
			if (has_directory)
			{
				return ResourceMgr::NormalizeAssetPath(normalized_stored_path, ResourceMgr::GetAssetPathDomain(asset_path));
			}

			WString asset_dir = asset_path;
			for (auto &ch: asset_dir)
			{
				if (ch == L'\\')
					ch = L'/';
			}
			asset_dir = asset_dir.substr(0, asset_dir.find_last_of(L"/") + 1);
			return asset_dir + normalized_stored_path;
		}

		WString MakeStoredExternalAssetPath(const WString &external_asset_path)
		{
			if (external_asset_path.empty() || PathUtils::IsSystemPath(external_asset_path))
				return external_asset_path;
			return PathUtils::GetFileName(external_asset_path, true);
		}

		WString FormatLogicalPath(WString path)
		{
			for (auto &ch: path)
			{
				if (ch == L'\\')
					ch = L'/';
			}
			return path;
		}

		void StripLeadingPathSeparators(WString &path)
		{
			while (!path.empty() && (path.front() == L'/' || path.front() == L'\\'))
				path.erase(path.begin());
		}

		WString NormalizeSysPathNoTrailingSlash(const WString &path)
		{
			WString normalized = PathUtils::FormatFilePath(path);
			while (!normalized.empty() && normalized.back() == L'/')
				normalized.pop_back();
			return normalized;
		}

		bool TryMakeRelativeAssetPath(const WString &sys_path, const WString &root_path, const WString &scheme, WString &out_asset_path)
		{
			if (root_path.empty())
				return false;

			const WString normalized_sys_path = NormalizeSysPathNoTrailingSlash(sys_path);
			const WString normalized_root_path = NormalizeSysPathNoTrailingSlash(root_path);
			if (normalized_sys_path == normalized_root_path)
			{
				out_asset_path = scheme;
				return true;
			}
			else
			{
				const WString root_prefix = PathUtils::NormalizeDirectoryPath(normalized_root_path);
				if (normalized_sys_path.compare(0, root_prefix.size(), root_prefix) != 0)
					return false;
			}

			std::error_code error;
			fs::path relative_path = fs::relative(fs::path(normalized_sys_path), fs::path(normalized_root_path), error);
			if (error)
				return false;

			WString relative_asset_path = FormatLogicalPath(relative_path.wstring());
			StripLeadingPathSeparators(relative_asset_path);
			out_asset_path = scheme + relative_asset_path;
			return true;
		}

		WString NormalizeLogicalDirectoryPath(const WString &path)
		{
			WString normalized = FormatLogicalPath(path);
			if (!normalized.empty() && normalized.back() != L'/')
				normalized.push_back(L'/');
			return normalized;
		}
	}// namespace

	void ResourceMgr::Init()
	{
		AL_ASSERT_MSG(g_pResourceMgr == nullptr, "ResourceMgr already init!");
		g_pResourceMgr = new ResourceMgr();
	}

	void ResourceMgr::Shutdown()
	{
		delete g_pResourceMgr; g_pResourceMgr = nullptr;
	}

	ResourceMgr &ResourceMgr::Get()
	{
		return *g_pResourceMgr;
	}

	WString ResourceMgr::GetResSysPath(const WString &p)
	{
		// 1. System path (e.g., C:/...) → format and return as-is
		if (PathUtils::IsSystemPath(p))
			return PathUtils::FormatFilePath(p);

		WString path = FormatLogicalPath(p);
		WString base_path;

		// 2. Check for known scheme prefixes, resolve to the corresponding root
		//     Order must match kPathScheme: { engine://, editor://, project:// }
		const WString *const kSchemeRoots[] = {
			&s_engine_res_root_path,
			&s_editor_res_root_path,
			&s_project_asset_root_path,
		};

		bool found_scheme = false;
		for (size_t i = 0; i < kPathScheme.size(); ++i)
		{
			if (path.starts_with(kPathScheme[i]))
			{
				base_path = *kSchemeRoots[i];
				path = path.substr(kPathScheme[i].length());
				found_scheme = true;
				break;
			}
		}

		if (!found_scheme)
		{
			// 3. Plain relative path — default to engine res root (backward compatible)
			base_path = s_engine_res_root_path;
		}

		// Strip leading slashes/backslashes from the path portion
		while (!path.empty() && (path.front() == L'/' || path.front() == L'\\'))
			path.erase(path.begin());

		return base_path + path;
	}

	WString ResourceMgr::GetResSysPath(EAssetDomain domain, const WString &relative_path)
	{
		if (domain == EAssetDomain::kRuntime)
		{
			LOG_ERROR("GetResSysPath: kRuntime domain has no scheme mapping");
			return relative_path;
		}
		return GetResSysPath(kPathScheme[static_cast<int>(domain)] + relative_path);
	}

	WString ResourceMgr::NormalizeAssetPath(const WString &asset_path, EAssetDomain default_domain)
	{
		if (asset_path.empty())
			return kEmptyWString;

		WString path = FormatLogicalPath(asset_path);
		if (PathUtils::IsSystemPath(path))
		{
			WString logical_path;
			if (TryMakeRelativeAssetPath(path, s_engine_res_root_path, kPathScheme[static_cast<int>(EAssetDomain::kEngine)], logical_path))
				return logical_path;
			if (TryMakeRelativeAssetPath(path, s_editor_res_root_path, kPathScheme[static_cast<int>(EAssetDomain::kEditor)], logical_path))
				return logical_path;
			if (TryMakeRelativeAssetPath(path, s_project_asset_root_path, kPathScheme[static_cast<int>(EAssetDomain::kProject)], logical_path))
				return logical_path;
			return path;
		}

		for (const auto &scheme: kPathScheme)
		{
			if (path.starts_with(scheme))
			{
				WString relative_path = path.substr(scheme.length());
				StripLeadingPathSeparators(relative_path);
				return scheme + relative_path;
			}
		}

		StripLeadingPathSeparators(path);
		if (default_domain == EAssetDomain::kRuntime)
			return path;
		return kPathScheme[static_cast<int>(default_domain)] + path;
	}

	EAssetDomain ResourceMgr::GetAssetPathDomain(const WString &asset_path, EAssetDomain default_domain)
	{
		const WString path = FormatLogicalPath(asset_path);
		for (size_t i = 0; i < kPathScheme.size(); ++i)
		{
			if (path.starts_with(kPathScheme[i]))
				return static_cast<EAssetDomain>(i);
		}

		if (PathUtils::IsSystemPath(path))
		{
			WString logical_path;
			if (TryMakeRelativeAssetPath(path, s_engine_res_root_path, kPathScheme[static_cast<int>(EAssetDomain::kEngine)], logical_path))
				return EAssetDomain::kEngine;
			if (TryMakeRelativeAssetPath(path, s_editor_res_root_path, kPathScheme[static_cast<int>(EAssetDomain::kEditor)], logical_path))
				return EAssetDomain::kEditor;
			if (TryMakeRelativeAssetPath(path, s_project_asset_root_path, kPathScheme[static_cast<int>(EAssetDomain::kProject)], logical_path))
				return EAssetDomain::kProject;
		}
		return default_domain;
	}

	void ResourceMgr::ConfigProject(Project *proj)
	{
		if (!proj)
		{
			LOG_ERROR("ResourceMgr::ConfigProject: proj is null");
			return;
		}
		s_project_root_path = PathUtils::NormalizeDirectoryPath(proj->RootDirectory());
		s_project_asset_root_path = PathUtils::NormalizeDirectoryPath(proj->AssetDirectory());
		s_project_library_root_path = PathUtils::NormalizeDirectoryPath(proj->LibraryDirectory());
		s_project_asset_database_path = s_project_library_root_path + L"AssetDatabase.json";
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

	int ResourceMgr::Initialize()
	{
		TimerBlock b("-----------------------------------------------------------ResourceMgr::Initialize");
		AL_ASSERT(!s_engine_res_root_path.empty());
		FileManager::SetCurPath(s_engine_res_root_path);
		_lut_global_resources_by_type[Material::StaticType()] = {};
		_lut_global_resources_by_type[Texture2D::StaticType()] = {};
		_lut_global_resources_by_type[Texture3D::StaticType()] = {};
		_lut_global_resources_by_type[Mesh::StaticType()] = {};
		_lut_global_resources_by_type[SkeletonMesh::StaticType()] = {};
		_lut_global_resources_by_type[Shader::StaticType()] = {};
		_lut_global_resources_by_type[ComputeShader::StaticType()] = {};
		_lut_global_resources_by_type[Scene::StaticType()] = {};
		_lut_global_resources_by_type[AnimationClip::StaticType()] = {};
		_lut_global_resources_by_type[Sprite::StaticType()] = {};
		_lut_global_resources_by_type[InputActionAsset::StaticType()] = {};
		_lut_global_resources_by_type[AudioClip::StaticType()] = {};
		_lut_global_resources_by_type[GraphAsset::StaticType()] = {};
		_lut_global_resources_by_type[ScriptAsset::StaticType()] = {};
		_asset_domains.emplace_back(AssetMountDesc{
			EAssetDomain::kEngine,
			kPathScheme[0],
			s_engine_res_root_path,
			s_engine_res_root_path + L"assetdb.alasset",
			true
		});
		_asset_domains.emplace_back(AssetMountDesc{
			EAssetDomain::kEditor,
			kPathScheme[1],
			s_editor_res_root_path,
			s_editor_res_root_path + L"assetdb.alasset",
			true
		});
		_asset_domains.emplace_back(AssetMountDesc{
			EAssetDomain::kProject,
			kPathScheme[2],
			s_project_asset_root_path,
			s_project_asset_database_path,
			true
		});
		LoadAssetDB(_asset_domains[0]);
		LoadAssetDB(_asset_domains[1]);
		LoadAssetDB(_asset_domains[2]);
		_asset_handler_registry.Register(MakeScope<SpriteAssetHandler>());
		_asset_handler_registry.Register(MakeScope<ScriptAssetHandler>());
		_asset_handler_registry.Register(MakeScope<ShaderAssetHandler>());
		_asset_handler_registry.Register(MakeScope<ComputeShaderAssetHandler>());
		_asset_handler_registry.Register(MakeScope<TextureAssetHandler>());
		_asset_handler_registry.Register(MakeScope<MaterialAssetHandler>());
		_asset_handler_registry.Register(MakeScope<MeshAssetHandler>());
		_asset_handler_registry.Register(MakeScope<SkeletonMeshAssetHandler>());
		_asset_handler_registry.Register(MakeScope<SceneAssetHandler>());
		_asset_handler_registry.Register(MakeScope<PrefabAssetHandler>());
		_asset_handler_registry.Register(MakeScope<AnimationClipAssetHandler>());
		_asset_handler_registry.Register(MakeScope<InputActionAssetHandler>());
		_asset_handler_registry.Register(MakeScope<AudioClipAssetHandler>());
		_asset_handler_registry.Register(MakeScope<GraphAssetHandler>());
		
		Vector<WString> shader_asset_pathes = {
				L"Shaders/hlsl/deferred_lighting.alasset",
				L"Shaders/hlsl/wireframe.alasset",
				L"Shaders/hlsl/gizmo.alasset",
				L"Shaders/hlsl/cubemap_gen.alasset",
				L"Shaders/hlsl/filter_irradiance.alasset",
				L"Shaders/hlsl/blit.alasset",
				L"Shaders/hlsl/skybox.alasset",
				L"Shaders/hlsl/PostProcess/bloom.alasset",
				L"Shaders/hlsl/forwardlit.alasset",
				L"Shaders/hlsl/default_ui.alasset",
				L"Shaders/hlsl/ui_shadow.alasset",
				L"Shaders/hlsl/default_text.alasset",
				L"Shaders/hlsl/voxel_drawer.alasset",
				L"Shaders/hlsl/texture3d_drawer.alasset",
				L"Shaders/hlsl/standard_volume.alasset",
				L"Shaders/hlsl/motion_vector.alasset",
				L"Shaders/hlsl/terrain.alasset",
				L"Shaders/hlsl/default_sprite.alasset",
				L"Shaders/hlsl/water.alasset"};
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
				L"Shaders/hlsl/sprite_pick_buffer.hlsl",
				L"Shaders/hlsl/sprite_select_buffer.hlsl",
		};

		//        std::atomic<int> shader_load_count = shader_asset_pathes.size() + shader_pathes.size();
		//		{
		//			Shader::s_p_defered_standart_lit = Load<Shader>(L"Shaders/hlsl/defered_standard_lit.alasset");
		//            for (auto &p: shader_asset_pathes)
		//                Core::ThreadPool::Get().Enqueue([&](WString p)
		//                                       { Load<Shader>(p); --shader_load_count ; }, p);
		//
		//            for (auto &p: shader_pathes)
		//                Core::ThreadPool::Get().Enqueue([&](WString p)
		//                                       { RegisterResource(p, LoadExternalShader(p)); --shader_load_count ; }, p);
		//			//RegisterResource(L"Shaders/hlsl/forwardlit.hlsl",LoadExternalShader(L"Shaders/hlsl/forwardlit.hlsl"));
		//
		//			Load<ComputeShader>(L"Shaders/hlsl/Compute/cs_mipmap_gen.alasset");
		//		}
		JobSystem::Get().Dispatch([](ResourceMgr *mgr)
								  { Shader::s_p_defered_standart_lit = mgr->Load<Shader>(L"Shaders/hlsl/defered_standard_lit.alasset"); },
								  this);
		Vector<WString> compute_shader_pathes = {
				L"Shaders/hlsl/Compute/cs_mipmap_gen.alasset",
				L"Shaders/hlsl/Compute/voxelize.alasset",
				L"Shaders/hlsl/Compute/ssao_cs.alasset",
				L"Shaders/hlsl/Compute/taa.alasset",
				L"Shaders/hlsl/Compute/hzb.alasset",
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
			JobSystem::Get().Dispatch([&](WString p)
									  { Load<ComputeShader>(p); }, p);
		JobSystem::Get().Wait();//防止加载mesh时，shader未加载完成
		{
			u8 *default_data = new u8[4 * 4 * 4];
			memset(default_data, 255, 64);
			auto default_white = Texture2D::Create(4, 4, ETextureFormat::kRGBA32);
			default_white->SetPixelData(default_data, 0);
			default_white->Name("default_white");
			default_white->Apply();
			RegisterResource(L"Runtime/default_white", default_white);

			memset(default_data, 0, 64);
			for (int i = 3; i < 64; i += 4)
				default_data[i] = 255;
			auto default_black = Texture2D::Create(4, 4, ETextureFormat::kRGBA32);
			default_black->SetPixelData(default_data, 0);
			default_black->Name("default_black");
			default_black->Apply();
			RegisterResource(L"Runtime/default_black", default_black);
			memset(default_data, 128, 64);
			for (int i = 3; i < 64; i += 4)
				default_data[i] = 255;
			auto default_gray = Texture2D::Create(4, 4, ETextureFormat::kRGBA32);
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
			auto default_normal = Texture2D::Create(4, 4, ETextureFormat::kRGBA32);
			default_normal->SetPixelData(default_data, 0);
			default_normal->Name("default_normal");
			default_normal->Apply();
			RegisterResource(L"Runtime/default_normal", default_normal);
			delete[] default_data; default_data = nullptr;
			Texture::s_p_default_white = default_white.get();
			Texture::s_p_default_black = default_black.get();
			Texture::s_p_default_gray = default_gray.get();
			Texture::s_p_default_normal = default_normal.get();
			//Load<Texture2D>(EnginePath::kEngineTexturePathW + L"small_cave_1k.alasset");
			TextureImportSetting setting;
			setting._is_sRGB = false;
			setting._generate_mipmap = false;
			auto lut1 = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"ltc_1.dds", setting);
			auto lut2 = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"ltc_2.dds", setting);
			RegisterResource(L"Runtime/ltc_lut1", lut1);
			RegisterResource(L"Runtime/ltc_lut2", lut2);
			auto noise = LoadExternalTexture(EnginePath::kEngineTexturePathW + L"rgba-noise-medium.png", setting);
			RegisterResource(L"Textures/noise_medium.png", noise);
			JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
									  { mgr->Load<Texture2D>(EnginePath::kEngineTexturePathW + L"blue_noise.alasset", &TextureImportSetting::Default()); },
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
										  RegisterResource(L"Textures/TerrainDiffuse", terrain_map); }, this);
		}
		WString mesh_path_cube = L"Meshs/src_res/cube.alasset";
		WString mesh_path_sphere = L"Meshs/src_res/sphere.alasset";
		WString mesh_path_plane = L"Meshs/src_res/plane.alasset";
		WString mesh_path_monkey = L"Meshs/src_res/monkey.alasset";
		WString mesh_path_capsule = L"Meshs/src_res/capsule.alasset";
		WString mesh_path_cone = L"Meshs/src_res/cone.alasset";
		WString mesh_path_cylinder = L"Meshs/src_res/cylinder.alasset";
		WString mesh_path_torus = L"Meshs/src_res/torus.alasset";

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
								  { Mesh::s_cube = mgr->Load<Mesh>(mesh_path_cube); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_sphere = mgr->Load<Mesh>(mesh_path_sphere); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_plane = mgr->Load<Mesh>(mesh_path_plane); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_capsule = mgr->Load<Mesh>(mesh_path_capsule); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_monkey = mgr->Load<Mesh>(mesh_path_monkey); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_cone = mgr->Load<Mesh>(mesh_path_cone); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_cylinder = mgr->Load<Mesh>(mesh_path_cylinder); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { Mesh::s_torus = mgr->Load<Mesh>(mesh_path_torus); },
								  this);
		JobSystem::Get().Dispatch([&](ResourceMgr *mgr)
								  { mgr->Load<Mesh>(L"Meshs/src_res/terrain_plane.alasset"); },
								  this);

		auto FullScreenQuad = MakeRef<Mesh>("FullScreenQuad");
		Vector<u32> indices = {0, 1, 2, 1, 3, 2};
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
			mat_creator(L"Shaders/hlsl/wireframe.alasset", L"Runtime/Material/Wireframe", "Wireframe");
			mat_creator(L"Shaders/hlsl/skybox.alasset", L"Runtime/Material/Skybox", "Skybox");
			mat_creator(L"Shaders/hlsl/cubemap_gen.alasset", L"Runtime/Material/CubemapGen", "CubemapGen");
			mat_creator(L"Shaders/hlsl/filter_irradiance.alasset", L"Runtime/Material/EnvmapFilter", "EnvmapFilter");
			mat_creator(L"Shaders/hlsl/blit.alasset", L"Runtime/Material/Blit", "Blit");
			mat_creator(L"Shaders/hlsl/gizmo.alasset", L"Runtime/Material/Gizmo", "GizmoDrawer");
			mat_creator(L"Shaders/hlsl/forwardlit.alasset", L"Runtime/Material/ForwardLit", "ForwardLit");
			mat_creator(L"Shaders/hlsl/texture3d_drawer.alasset", L"Runtime/Material/Texture3dDrawer", "Texture3dDrawer");
			Material::s_standard_forward_lit = GetRef<Material>(L"Runtime/Material/ForwardLit");
			Material::s_standard_forward_lit.lock()->SetVector("_AlbedoValue", Colors::kWhite);
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
		SaveAssetDB(EAssetDomain::kProject);
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
			LOG_WARNING("SaveAsset: Asset: {} save failed!it hasn't a instanced object!", asset->Name());
			return;
		}
		AL_ASSERT(!asset->_asset_path.empty());

		IAssetHandler *handler = FindAssetHandler(asset->_asset_type);
		if (handler != nullptr)
		{
			AssetSaveContext ctx;
			ctx._asset = asset;
			ctx._asset_path = asset->_asset_path;
			ctx._resource_mgr = this;
			ctx._system_path = GetResSysPath(asset->_asset_path);
			handler->Save(ctx);
			return;
		}

		AL_ASSERT_MSG(false, "SaveAsset: No handler registered for asset type {}", GetAssetTypeName(asset->_asset_type));
	}

	void ResourceMgr::SaveAllUnsavedAssets()
	{
		std::lock_guard<std::mutex> lock(_asset_db_mutex);
		while (!s_pending_save_assets.empty())
		{
			SaveAsset(s_pending_save_assets.front());
			s_pending_save_assets.pop();
		}
		SaveAssetDB(EAssetDomain::kProject);
	}

	void ResourceMgr::MigrateLegacyAssetDocuments(const WString &root_asset_dir)
	{

	}

	Asset *ResourceMgr::GetLinkedAsset(Object *obj)
	{
		if (_object_to_asset.contains(obj->ID()))
			return _object_to_asset[obj->ID()];
		return nullptr;
	}


	void ResourceMgr::ConfigEditorResRoot(const WString &root)
	{
		s_editor_res_root_path = PathUtils::NormalizeDirectoryPath(root);
	}

	void ResourceMgr::ConfigEngineResRoot(const WString &root)
	{
		s_engine_res_root_path = PathUtils::NormalizeDirectoryPath(root);
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

	Ref<Texture2D> ResourceMgr::LoadExternalTexture(const WString &asset_path, const ImportSetting &settings)
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
		else
		{
		};
		AL_ASSERT(tex_parser != nullptr);
		LOG_INFO(L"Start load image file {}...", sys_path);
		auto tex = tex_parser->Parser(sys_path, dynamic_cast<const TextureImportSetting &>(settings));
		tex->Apply();
		return tex;
	}
	bool ResourceMgr::LoadExternalTexture(const WString &asset_path, Ref<Texture2D> &tex, const ImportSetting &settings)
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
		else
		{
		};
		AL_ASSERT(tex_parser != nullptr);
		LOG_INFO(L"Start load image file {}...", sys_path);
		bool ret = tex_parser->Parser(sys_path, tex, dynamic_cast<const TextureImportSetting &>(settings));
		tex->Apply();
		return ret;
	}

	Ref<Object> ResourceMgr::Load(const WString& asset_path,const ImportSetting* settings,const Type* type)
	{
        const WString normalized_asset_path = NormalizeAssetPath(asset_path);
        if (normalized_asset_path.empty())
        {
            LOG_ERROR(L"ResourceMgr::Load: Asset path is empty");
            return nullptr;
        }
        LOG_WARNING(L"Begin load asset {}...", normalized_asset_path);
        TimeMgr timer;
        timer.Mark();
        //using Loader = std::function<Scope<Asset>(ResourceMgr *, const WString &,const ImportSetting&)>;
        WString sys_path = ResourceMgr::GetResSysPath(normalized_asset_path);
        auto ext = PathUtils::ExtractExt(sys_path);
        bool is_engine_asset = ext == L".alasset" || ext == L".almap";
        AL_ASSERT(is_engine_asset);
		AssetDocumentHeader header;
		if (!TryLoadAssetDocumentHeader(sys_path,header))
			return nullptr;
		auto src_type = Type::Find(header._asset_type);
        auto asset_handler = _asset_handler_registry.Find(type);
        if (type == nullptr || !IsTypeCompatible(src_type, type))
        {
            LOG_ERROR(L"Load asset {} failed with mismatched asset type {} after {}ms", normalized_asset_path, GetAssetTypeName(type), timer.GetElapsedSinceLastMark());
            return nullptr;
        }
        if (asset_handler == nullptr)
        {
            LOG_ERROR(L"Load asset {} failed because no loader is registered for asset type {} after {}ms", normalized_asset_path, GetAssetTypeName(type), timer.GetElapsedSinceLastMark());
            return nullptr;
        }
        // bool is_skip_load = false;
        // if (!IsFileOnDiskUpdated(sys_path))
        // {
        //     LogMgr::Get().LogWarningFormat(L"Load asset {} succeed with everything is new after {}ms", asset_path, timer.GetElapsedSinceLastMark());
        //     return _global_resources.contains(asset_path) ? std::static_pointer_cast<T>(_global_resources[asset_path]) : nullptr;
        // }
		if (settings == nullptr)
			settings = &ImportSetting::Default();
		AssetLoadContext load_ctx;
		load_ctx._asset_path = asset_path;
		load_ctx._resource_mgr = this;
		load_ctx._system_path = sys_path;
		load_ctx._import_setting = settings;
		if (type == StaticClass<Render::Texture2D>())
			load_ctx._import_setting = &TextureImportSetting::Default();

        if (!IsAssetLoaded(normalized_asset_path))
        {
            Scope<Asset> out_asset;
            out_asset = std::move(asset_handler->Load(load_ctx));
            if (out_asset != nullptr)
            {
                RegisterResource(normalized_asset_path, out_asset->_p_obj);
                RegisterAsset(std::move(out_asset));
                MarkFileTimeStamp(sys_path);
                LOG_WARNING(L"Load asset {} succeed after {} ms", normalized_asset_path, timer.GetElapsedSinceLastMark());
            }
            else
            LOG_ERROR(L"Load asset {} failed after {} ms", normalized_asset_path, timer.GetElapsedSinceLastMark());
        }
        else
        {
            if (settings->_is_reimport)
            {
                asset_handler->Load(load_ctx);
                MarkFileTimeStamp(sys_path);
                LOG_WARNING(L"Reload asset {} after {} ms", normalized_asset_path, timer.GetElapsedSinceLastMark());
            }
        }

        return _global_resources.contains(normalized_asset_path) ? _global_resources[normalized_asset_path] : nullptr;
	}

	Asset *ResourceMgr::CreateAsset(const WString &asset_path, Ref<Object> obj, bool overwrite)
	{
		const WString normalized_asset_path = NormalizeAssetPath(asset_path);
		bool is_already_exist = _asset_looktable.contains(normalized_asset_path);
		if (is_already_exist && !overwrite)
		{
			LOG_WARNING(L"CreateAsset: Asset: {} already exist!", normalized_asset_path);
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
		const Type *asset_type = obj != nullptr ? obj->GetType() : nullptr;
		AL_ASSERT(asset_type != nullptr);
		new_asset = MakeScope<Asset>(new_guid, asset_type, normalized_asset_path);
		new_asset->_domain = GetAssetPathDomain(normalized_asset_path, EAssetDomain::kProject);
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
		RegisterResource(normalized_asset_path, obj);
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
		return _asset_looktable.contains(NormalizeAssetPath(asset_path));
	}

	const WString &ResourceMgr::GetAssetPath(Object *obj) const
	{
		if (_object_to_asset.contains(obj->ID()))
		{
			return _object_to_asset.at(obj->ID())->_asset_path;
		}
		return kEmptyWString;
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
		return kEmptyWString;
	}

	Asset *ResourceMgr::GetAsset(const WString &asset_path) const
	{
		const WString normalized_asset_path = NormalizeAssetPath(asset_path);
		if (_asset_looktable.contains(normalized_asset_path))
		{
			return _asset_db.contains(_asset_looktable.at(normalized_asset_path)) ? _asset_db.at(_asset_looktable.at(normalized_asset_path)).get() : nullptr;
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
		p_asset->Name(ToChar(new_name));
		p_asset->_asset_path = new_asset_path;
		RegisterAsset(std::move(asset));
		//p_asset->_p_obj->Name(ToChar(new_name));
		return true;
	}

	bool ResourceMgr::MoveAsset(Asset *p_asset, const WString &new_asset_path)
	{
		WString old_asset_path = p_asset->_asset_path;
		WString normalized_new_asset_path = NormalizeAssetPath(new_asset_path, p_asset->_domain);
		if (ExistInAssetDB(normalized_new_asset_path))
		{
			LOG_WARNING(L"MoveAsset to path {} failed,try another name!", normalized_new_asset_path);
			return false;
		}
		UnRegisterResource(old_asset_path);
		RegisterResource(normalized_new_asset_path, p_asset->_p_obj);
		Scope<Asset> asset = std::move(_asset_db.at(p_asset->GetGuid()));
		UnRegisterAsset(p_asset);
		p_asset->_asset_path = normalized_new_asset_path;
		p_asset->_domain = GetAssetPathDomain(normalized_new_asset_path, p_asset->_domain);
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
		const WString normalized_asset_path = NormalizeAssetPath(asset_path);
		if (ExistInAssetDB(normalized_asset_path))
		{
			return _global_resources.contains(normalized_asset_path);
		}
		return false;
	}

	void ResourceMgr::LoadAssetDB(const AssetMountDomain& domain)
	{
		JsonArchive ar;
		ar.Load(domain._database_path);
		if (!ar.IsLoaded())
		{
			LOG_ERROR(L"Load asset_db with path: {} failed!", domain._database_path);
			return;
		}

		// Navigate into the "assets" array
		ar.BeginObject("assets");
		FStructedArchive::EStructedDataType type;
		u32 count = ar.BeginArray(type);
		for (u32 i = 0; i < count; ++i)
		{
			String guid_str, asset_path_str, type_name;

			ar.BeginObject(std::to_string(i));

			ar.BeginObject("guid");
			ar.ReadString(guid_str);
			ar.EndObject();

			ar.BeginObject("path");
			ar.ReadString(asset_path_str);
			ar.EndObject();

			ar.BeginObject("type");
			ar.ReadString(type_name);
			ar.EndObject();

			ar.EndObject(); // end of array item

			WString asset_path = NormalizeAssetPath(ToWChar(asset_path_str), domain._domain);

			const Type *asset_type = FindAssetType(ToWChar(type_name));
			auto asset = MakeScope<Asset>(Guid(guid_str), asset_type, asset_path);
			asset->Name(ToChar(PathUtils::GetFileName(asset_path).c_str()));
			asset->_domain = domain._domain;
			//先占位，不进行资源加载，实际有使用时才加载。
			RegisterAsset(std::move(asset));
		}
		ar.EndArray();
		ar.EndObject();
	}

	void ResourceMgr::SaveAssetDB(EAssetDomain domain)
	{
		// Find the matching AssetMountDomain
		const AssetMountDomain *mount_domain = nullptr;
		for (auto &d: _asset_domains)
		{
			if (d._domain == domain)
			{
				mount_domain = &d;
				break;
			}
		}
		if (mount_domain == nullptr)
		{
			LOG_ERROR("SaveAssetDB: domain {} not found in _asset_domains", (int)domain);
			return;
		}

		// Collect assets belonging to this domain
		Vector<std::pair<Guid, const Asset *>> domain_assets;
		for (auto &[guid, asset]: _asset_db)
		{
			if (asset->_domain == domain)
				domain_assets.emplace_back(guid, asset.get());
		}

		JsonArchive ar;
		ar.BeginObject("assets");
		ar.BeginArray(domain_assets.size(), FStructedArchive::EStructedDataType::kStruct);

		for (size_t i = 0; i < domain_assets.size(); ++i)
		{
			auto &[guid, asset] = domain_assets[i];

			ar.BeginObject(std::to_string(i));

			ar.BeginObject("guid");
			ar.WriteString(guid.ToString());
			ar.EndObject();

			ar.BeginObject("path");
			ar.WriteString(ToChar(NormalizeAssetPath(asset->_asset_path, asset->_domain)));
			ar.EndObject();

			ar.BeginObject("type");
			ar.WriteString(asset->_asset_type ? asset->_asset_type->FullName() : String{});
			ar.EndObject();

			ar.EndObject(); // end of array item
		}

		ar.EndArray();
		ar.EndObject();
		ar.Save(mount_domain->_database_path);
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
					if (tex)
						RegisterResource(asset_path, tex);
				}
				if (tex)
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
					if (tex)
						RegisterResource(asset_path, tex);
				}
				if (tex)
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
					if (tex)
						RegisterResource(asset_path, tex);
				}
				if (tex)
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
		asset->_asset_path = NormalizeAssetPath(asset->_asset_path, asset->_domain);
		asset->_domain = GetAssetPathDomain(asset->_asset_path, asset->_domain);

		auto is_exist = ExistInAssetDB(asset.get());
		if (is_exist && !override)
		{
			LOG_WARNING(L"Asset {} already exist in database,it will be destory...", asset->_asset_path);
			return nullptr;
		}
		if (is_exist)
		{
			auto exist_asset = _asset_db[asset->GetGuid()].get();
			if (exist_asset->_asset_path != asset->_asset_path)
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
			auto resource_type = obj->GetType();
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
			auto resource_type = obj->GetType();
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
		return ImportResourceImpl(sys_path, target_dir, &setting);
	}
	void ResourceMgr::SubmitTaskSync(ResourceTask task)
	{
		_sync_tasks.push([=]()
						 { task(); });
	}

	void ResourceMgr::SubmitTaskSync(ResourceTask task, std::function<void(bool)> callback)
	{
		_sync_tasks.push([=]()
						 { callback(task()); });
	}

	Ref<void> ResourceMgr::ImportResourceAsync(const WString &sys_path, const WString &target_dir, const ImportSetting &setting, OnResourceTaskCompleted callback)
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
		fs::path p(sys_path), dir(target_dir);
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
		if (ext.empty() || (!kHDRImageExt.contains(ext) && !kLDRImageExt.contains(ext) && !kMeshExt.contains(ext) && !kAudioExt.contains(ext)))
		{
			LOG_ERROR(L"Path {} is not a supported file!", sys_path);
			return nullptr;
		}
		const WString normalized_source_path = NormalizeAssetPath(sys_path, EAssetDomain::kRuntime);
		bool source_is_asset_path = false;
		for (const auto &scheme: kPathScheme)
		{
			if (normalized_source_path.starts_with(scheme))
			{
				source_is_asset_path = true;
				break;
			}
		}
		if (source_is_asset_path && !IsFileOnDiskUpdated(sys_path))
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
		WString external_asset_path = NormalizeAssetPath(res_copy_path.wstring(), GetAssetPathDomain(target_dir, EAssetDomain::kProject));
		TimeMgr time_mgr;
		Ref<void> ret_res = nullptr;
		Queue<std::tuple<WString, Ref<Object>>> loaded_objects;
		time_mgr.Mark();
		WString created_asset_dir = NormalizeLogicalDirectoryPath(NormalizeAssetPath(target_dir, GetAssetPathDomain(target_dir, EAssetDomain::kProject)));
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
			auto tex = LoadExternalTexture(external_asset_path, *tex_import_setting);
			WString imported_asset_path = created_asset_dir;
			imported_asset_path.append(std::format(L"{}.alasset", ToWChar(tex->Name().c_str())));
			loaded_objects.push(std::make_tuple(imported_asset_path, tex));
		}
		else if (kAudioExt.contains(ext))
		{
            auto clip = MakeRef<AudioClip>();
            clip->Name(ToChar(p.stem().wstring()));
			clip->_runtime_path = ToChar(ResourceMgr::GetResSysPath(external_asset_path));
			clip->_load_mode = EAudioLoadMode::kMemory;
			WString imported_asset_path = created_asset_dir;
			imported_asset_path.append(std::format(L"{}.alasset", ToWChar(clip->Name().c_str())));
			loaded_objects.push(std::make_tuple(imported_asset_path, clip));
		}
		else
		{
		}
		while (!loaded_objects.empty())
		{
			auto &[path, obj] = loaded_objects.front();
			auto new_asset = CreateAsset(path, obj);
			new_asset->_external_asset_path = MakeStoredExternalAssetPath(external_asset_path);
			if (obj->GetType() == Mesh::StaticType() || obj->GetType() == SkeletonMesh::StaticType())
			{
				auto mesh_import_setting = dynamic_cast<const MeshImportSetting *>(resolved_setting);
				mesh_import_setting = mesh_import_setting ? mesh_import_setting : &MeshImportSetting::Default();
				_importers[new_asset->_asset_path] = AL_NEW(MeshImportSetting, (*mesh_import_setting));
			}
			else if (obj->GetType() == Texture2D::StaticType() || obj->GetType() == Texture3D::StaticType())
			{
				auto tex_import_setting = dynamic_cast<const TextureImportSetting *>(resolved_setting);
				tex_import_setting = tex_import_setting ? tex_import_setting : &TextureImportSetting::Default();
				_importers[new_asset->_asset_path] = AL_NEW(TextureImportSetting, (*tex_import_setting));
			}
			LOG_INFO(L"Create asset at path {}", path);
			loaded_objects.pop();
		}
		OnAssetDataBaseChanged();
		SaveAllUnsavedAssets();
		return ret_res;
	}

	IAssetHandler *ResourceMgr::FindAssetHandler(const Type *asset_type) const
	{
		return _asset_handler_registry.Find(asset_type);
	}

	ImportSetting *ResourceMgr::GetImportSetting(const WString &asset_path) const
	{
		auto it = _importers.find(asset_path);
		return it != _importers.end() ? it->second : nullptr;
	}

	void ResourceMgr::SetImportSetting(const WString &asset_path, ImportSetting *setting)
	{
		// Clean up old setting if it exists
		auto it = _importers.find(asset_path);
		if (it != _importers.end() && it->second != setting)
		{
			AL_DELETE(it->second);
		}
		_importers[asset_path] = setting;
	}

	void ResourceMgr::OnAssetDataBaseChanged()
	{
		for (auto &f: _asset_changed_callbacks)
		{
			f();
		}
	}
}// namespace Ailu
