#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "GlobalMarco.h"
#include "Framework/Common/LogMgr.h"
#include "Framework/Common/FileManager.h"
#include "Render/GraphicsContext.h"

#include "RHI/DX12/D3DTexture.h"
#include "Framework/Math/Random.h"
#include "Render/GraphicsPipelineStateObject.h"


namespace Ailu
{
	namespace fs = std::filesystem;

	//填充监听路径下的所有文件
	static void TraverseDirectory(const fs::path& directoryPath, std::set<fs::path>& path_set)
	{
		for (const auto& entry : fs::directory_iterator(directoryPath))
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

	int ResourceMgr::Initialize()
	{
		for (int i = 0; i < EAssetType::COUNT; i++)
		{
			_lut_global_resources_by_type[EAssetType::FromString(EAssetType::_Strings[i])] = Vector<ResourcePoolContainer::iterator>();
		}
		_project_root_path = kEngineResRootPathW.substr(0, kEngineResRootPathW.find_last_of(L"/"));
		LoadAssetDB();
		{
			Shader::s_p_defered_standart_lit = Load<Shader>(L"Shaders/defered_standard_lit.alasset");
			Load<Shader>(L"Shaders/deferred_lighting.alasset");
			Load<Shader>(L"Shaders/wireframe.alasset");
			Load<Shader>(L"Shaders/gizmo.alasset");
			Load<Shader>(L"Shaders/depth_only.alasset");
			Load<Shader>(L"Shaders/cubemap_gen.alasset");
			Load<Shader>(L"Shaders/filter_irradiance.alasset");
			Load<Shader>(L"Shaders/blit.alasset");
			Load<Shader>(L"Shaders/skybox.alasset");
			Load<Shader>(L"Shaders/bloom.alasset");
			Load<Shader>(L"Shaders/forwardlit.alasset");
			RegisterResource(L"Shaders/hlsl/debug.hlsl", LoadExternalShader(L"Shaders/hlsl/debug.hlsl"));
			RegisterResource(L"Shaders/hlsl/billboard.hlsl", LoadExternalShader(L"Shaders/hlsl/billboard.hlsl"));
			//RegisterResource(L"Shaders/hlsl/forwardlit.hlsl",LoadExternalShader(L"Shaders/hlsl/forwardlit.hlsl"));
			GraphicsPipelineStateMgr::BuildPSOCache();
			Load<ComputeShader>(L"Shaders/cs_mipmap_gen.alasset");
		}
		{
			u8* default_data = new u8[4 * 4 * 4];
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
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"folder.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"file.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"3d.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"shader.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"image.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"dark/material.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"scene.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"point_light.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"directional_light.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"spot_light.alasset");
			Load<Texture2D>(EnginePath::kEngineIconPathW + L"camera.alasset");
			Load<Texture2D>(EnginePath::kEngineTexturePathW + L"ibl_brdf_lut.alasset");
			Load<Texture2D>(EnginePath::kEngineTexturePathW + L"small_cave_1k.alasset");
		}
		{
			Load<Material>(L"Materials/StandardPBR.alasset");
			auto mat_creator = [this](const WString& shader_path, const WString& mat_path, const String& mat_name) {
				RegisterResource(mat_path, MakeRef<Material>(Get<Shader>(shader_path), mat_name));
				};
			mat_creator(L"Shaders/skybox.alasset", L"Runtime/Material/Skybox", "Skybox");
			mat_creator(L"Shaders/cubemap_gen.alasset", L"Runtime/Material/CubemapGen", "CubemapGen");
			mat_creator(L"Shaders/filter_irradiance.alasset", L"Runtime/Material/EnvmapFilter", "EnvmapFilter");
			mat_creator(L"Shaders/blit.alasset", L"Runtime/Material/Blit", "Blit");
			mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/PointLightBillboard", "PointLightBillboard");
			mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/DirectionalLightBillboard", "DirectionalLightBillboard");
			mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/SpotLightBillboard", "SpotLightBillboard");
			mat_creator(L"Shaders/hlsl/billboard.hlsl", L"Runtime/Material/CameraBillboard", "CameraBillboard");
			g_pResourceMgr->Get<Material>(L"Runtime/Material/PointLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"point_light.alasset");
			g_pResourceMgr->Get<Material>(L"Runtime/Material/DirectionalLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"directional_light.alasset");
			g_pResourceMgr->Get<Material>(L"Runtime/Material/SpotLightBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"spot_light.alasset");
			g_pResourceMgr->Get<Material>(L"Runtime/Material/CameraBillboard")->SetTexture("_MainTex", EnginePath::kEngineIconPathW + L"camera.alasset");
		}

		Load<Mesh>(L"Meshs/sphere.alasset");
		Load<Mesh>(L"Meshs/plane.alasset");
		Load<Mesh>(L"Meshs/cube.alasset");
		Load<Mesh>(L"Meshs/monkey.alasset");

		Mesh::s_p_cube = static_cast<Mesh*>(_global_resources[L"Meshs/cube.alasset"].get());
		Mesh::s_p_shpere = static_cast<Mesh*>(_global_resources[L"Meshs/sphere.alasset"].get());
		Mesh::s_p_plane = static_cast<Mesh*>(_global_resources[L"Meshs/plane.alasset"].get());

		auto FullScreenQuad = MakeRef<Mesh>("FullScreenQuad");
		Vector3f* vertices = new Vector3f[4]{ { -1.0f, 1.0f, 0.0f },
		{  1.0f, 1.0f, 0.0f },
		{ -1.0f,  -1.0f, 0.0f },
		{  1.0f,  -1.0f, 0.0f } };
		u32* indices = new u32[6]{ 0, 1, 2, 1, 3, 2 };
		Vector2f* uv0 = new Vector2f[4]{ {0.f,0.f}, {1.f,0.f},{0.f,1.f} ,{1.f,1.f} };
		FullScreenQuad->_vertex_count = 4;
		FullScreenQuad->SetVertices(vertices);
		FullScreenQuad->AddSubmesh(indices, 6);
		FullScreenQuad->SetUVs(uv0, 0);
		FullScreenQuad->BuildRHIResource();
		RegisterResource(L"Runtime/Mesh/FullScreenQuad", FullScreenQuad);
		Mesh::s_p_quad = FullScreenQuad.get();
		g_pThreadTool->Enqueue(&ResourceMgr::WatchDirectory, this);
		return 0;
	}

	void ResourceMgr::Finalize()
	{
		_is_watching_directory = false;
		for (auto& [guid, asset] : _asset_db)
		{
			SaveAsset(asset.get());
		}
		SaveAssetDB();
	}

	void ResourceMgr::Tick(const float& delta_time)
	{
		while (!_pending_delete_assets.empty())
		{
			Asset* asset = _pending_delete_assets.front();
			auto asset_sys_path = PathUtils::GetResSysPath(asset->_asset_path);
			if (s_file_last_load_time.contains(asset_sys_path))
			{
				s_file_last_load_time.erase(asset_sys_path);
			}
			UnRegisterResource(asset->_asset_path);
			UnRegisterAsset(asset);
			_pending_delete_assets.pop();
		}
		while (!_shader_waiting_for_compile.empty())
		{
			Shader* shader = _shader_waiting_for_compile.front();
			if (shader->PreProcessShader())
			{
				AddResourceTask([=]()->Ref<void> {
					for (auto& mat : shader->GetAllReferencedMaterials())
					{
						mat->ConstructKeywords(shader);
						bool is_all_succeed = true;
						for (u16 i = 0; i < shader->PassCount(); i++)
						{
							is_all_succeed &= shader->Compile(i,mat->ActiveVariantHash(i));
						}
						if (is_all_succeed)
						{
							mat->ChangeShader(shader);
						}
					}
					return nullptr;
					});
			}
			_shader_waiting_for_compile.pop();
		}
		while (!_compute_shader_waiting_for_compile.empty())
		{
			ComputeShader* shader = _compute_shader_waiting_for_compile.front();
			shader->_is_compiling.store(true);// shader Compile()也会设置这个值，这里设置一下防止读取该值时还没执行compile
			AddResourceTask([=]()->void* {
				shader->Compile();
				return nullptr;
				});
			_compute_shader_waiting_for_compile.pop();
		}
		SubmitResourceTask();
	}

	void ResourceMgr::SaveAsset(const Asset* asset)
	{
		using std::endl;
		if (asset->_p_obj == nullptr)
		{
			g_pLogMgr->LogWarningFormat(L"SaveAsset: Asset: {} save failed!it hasn't a instanced object!", asset->_name);
			return;
		}
		AL_ASSERT(asset->_asset_path.empty());
		//写入公共头
		auto sys_path = PathUtils::GetResSysPath(asset->_asset_path);
		std::wofstream out_asset_file(sys_path, std::ios::out | std::ios::trunc);
		AL_ASSERT(!out_asset_file.is_open());
		out_asset_file << "guid: " << ToWChar(asset->GetGuid().ToString()) << endl;
		out_asset_file << "type: " << EAssetType::ToString(asset->_asset_type) << endl;
		out_asset_file << "name: " << ToWChar(asset->_p_obj->Name()) << endl;
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
		}
		return;
		}
		AL_ASSERT(true);
	}

	void ResourceMgr::SaveAllUnsavedAssets()
	{
        std::lock_guard<std::mutex> lock(_asset_db_mutex);
		while (!s_pending_save_assets.empty())
		{
			SaveAsset(s_pending_save_assets.front());
			s_pending_save_assets.pop();
		}
	}

	Asset* ResourceMgr::GetLinkedAsset(Object* obj)
	{
		if (_object_to_asset.contains(obj->ID()))
			return _object_to_asset[obj->ID()];
		return nullptr;
	}

	Ref<Shader> ResourceMgr::LoadExternalShader(const WString& asset_path, const ImportSetting* setting)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		auto s_setting = dynamic_cast<const ShaderImportSetting*>(setting);
		if (setting)
		{
			return Shader::Create(sys_path);
		}
		return Shader::Create(sys_path);
	}

	Ref<ComputeShader> ResourceMgr::LoadExternalComputeShader(const WString& asset_path, const ImportSetting* setting)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		auto s_setting = dynamic_cast<const ShaderImportSetting*>(setting);
		if (setting)
		{
			return ComputeShader::Create(sys_path);
		}
		return ComputeShader::Create(sys_path);
	}

	void ResourceMgr::SaveShader(const WString& asset_path, const Asset* asset)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
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

	void ResourceMgr::SaveComputeShader(const WString& asset_path, const Asset* asset)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		if (FileManager::Exist(sys_path))
		{
			auto cs = asset->As<ComputeShader>();
			String kernel = cs->KernelName();
			std::wstringstream wss;
			WString indent = L"  ";
			wss << indent << L"file: " << asset->_external_asset_path << std::endl;
			wss << indent << L"kernel: " << ToWStr(kernel.c_str()) << std::endl;
			if (FileManager::WriteFile(sys_path, true, wss.str()))
			{
				return;
			}
		}
		g_pLogMgr->LogErrorFormat(L"Save compute shader to {} failed!", sys_path);
	}

	Scope<Asset> ResourceMgr::LoadShader(const WString& asset_path)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		WString data;
		Ref<Shader>	shader;
		if (FileManager::ReadFile(sys_path, data))
		{
			auto c = StringUtils::Split(data, L"\n");
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
			asset->_p_obj = LoadExternalShader(file, &setting);
			return asset;
		}
		return nullptr;
	}

	void ResourceMgr::SaveMaterial(const WString& asset_path, Material* mat)
	{
		using std::endl;
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		std::multimap<std::string, SerializableProperty*> props{};
		//为了写入通用资产头信息，暂时使用追加方式打开
		std::ofstream out_mat(sys_path, std::ios::out | std::ios::app);
		out_mat << "shader_guid: " << GetAssetGuid(mat->_p_shader).ToString() << endl;
		out_mat << "keywords: " << su::Join(mat->_all_keywords,",") << endl;
		for (auto& prop : mat->_properties)
		{
			props.insert(std::make_pair(GetSerializablePropertyTypeStr(prop.second._type), &prop.second));
		}
		//out_mat << endl;
		auto float_props = mat->GetAllFloatValue();
		auto vector_props = mat->GetAllVectorValue();
		auto uint_props = mat->GetAllUintValue();
		//auto tex_props = mat->GetAllTexture();
		out_mat << "  prop_type: " << "Uint" << endl;
		for (auto& [name, value] : uint_props)
		{
			out_mat << "    " << name << ": " << value << endl;
		}
		out_mat << "  prop_type: " << "Float" << endl;
		for (auto& [name, value] : float_props)
		{
			out_mat << "    " << name << ": " << value << endl;
		}
		out_mat << "  prop_type: " << "Vector" << endl;
		for (auto& [name, value] : vector_props)
		{
			out_mat << "    " << name << ": " << value << endl;
		}
		out_mat << "  prop_type: " << "Texture2D" << endl;
		for (auto& [prop_name, prop] : props)
		{
			if (prop->_type == ESerializablePropertyType::kTexture2D)
			{
				auto tex_opti = SerializableProperty::GetProppertyValue<std::tuple<u8, Texture*>>(*prop);
				Guid tex_guid;
				if (tex_opti.has_value() && std::get<1>(tex_opti.value()))
				{
					auto [bind_slot, tex] = tex_opti.value();
                    Asset* lined_asset = GetLinkedAsset(tex);
					if (lined_asset)
						tex_guid = lined_asset->GetGuid();
				}
				else
					tex_guid = Guid::EmptyGuid();
				out_mat << "    " << prop->_value_name << ": " << tex_guid.ToString() << endl;
			}
		}
		out_mat.close();
		LOG_WARNING(L"Save material to {}", sys_path);
	}

	void ResourceMgr::SaveMesh(const WString& asset_path, const Asset* asset)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
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

	void ResourceMgr::SaveTexture2D(const WString& asset_path, const Asset* asset)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
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

	void ResourceMgr::SaveScene(const WString& asset_path, const Asset* asset)
	{
		Scene* scene = asset->As<Scene>();
		using namespace std;
		std::ostringstream ss;
		try
		{
			String level1 = GetIndentation(1), level2 = GetIndentation(2), level3 = GetIndentation(3), level4 = GetIndentation(4);
			//ss << "SceneName: " << scene->Name() << endl;
			ss << "SceneGraph: " << endl;
			scene->Root()->Serialize(ss, level1);
		}
		catch (const std::exception&)
		{
			g_pLogMgr->LogErrorFormat("Serialize failed when save scene: {}!", scene->Name());
			return;
		}
		WString sys_path = PathUtils::GetResSysPath(asset->_asset_path);
		if (!FileManager::WriteFile(sys_path, true, ss.str()))
		{
			g_pLogMgr->LogErrorFormat(L"Save scene failed to {}", sys_path);
			return;
		}
		g_pLogMgr->LogFormat(L"Save scene to {}", sys_path);
	}

	List<Ref<Mesh>> ResourceMgr::LoadExternalMesh(const WString& asset_path)
	{
		static auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		auto mesh_list = parser->Parser(sys_path);
		auto& imported_mat_infos = parser->GetImportedMaterialInfos();
		for (auto& mesh : mesh_list)
		{
			for (auto it = imported_mat_infos.begin(); it != imported_mat_infos.end(); ++it)
			{
				mesh->AddCacheMaterial(*it);
			}
			g_pGfxContext->SubmitRHIResourceBuildTask([=]() {mesh->BuildRHIResource(); });
		}
		return mesh_list;
	}

	Ref<Texture2D> ResourceMgr::LoadExternalTexture(const WString& asset_path, const ImportSetting* setting)
	{
		auto image_import_setting = dynamic_cast<const TextureImportSetting*>(setting);
		image_import_setting = image_import_setting ? image_import_setting : &TextureImportSetting::Default();
		auto sys_path = PathUtils::GetResSysPath(asset_path);
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
		else {};
		AL_ASSERT(tex_parser == nullptr);
		g_pLogMgr->LogFormat(L"Start load image file {}...", sys_path);
		auto tex = tex_parser->Parser(sys_path);
		g_pGfxContext->SubmitRHIResourceBuildTask([=]() {tex->Apply(); });
		return tex;
	}

	Scope<Asset> ResourceMgr::LoadTexture(const WString& asset_path)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		WString data;
		Ref<Texture2D>	shader;
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

	Scope<Asset> ResourceMgr::LoadMaterial(const WString& asset_path)
	{
		WString sys_path = PathUtils::GetResSysPath(asset_path);
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
		AL_ASSERT_MSG(line_count <= 3, "material file error");
		String key{}, guid_str{}, type_str{}, name{}, shader_guid{},keywords;
		FormatLine(lines[0], key, guid_str);
		FormatLine(lines[1], key, type_str);
		FormatLine(lines[2], key, name);
		FormatLine(lines[3], key, shader_guid);
		FormatLine(lines[4], key, keywords);
		Shader* shader = Get<Shader>(Guid(shader_guid));
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
		//mat->SurfaceType(ESurfaceType::FromString(surface_type));
		mat->_all_keywords.clear();
		for (auto& kw : su::Split(keywords, ","))
		{
			mat->_all_keywords.insert(kw);
		}
		mat->ConstructKeywords(mat->_p_shader);
		std::string cur_type{ " " };
		std::string prop_type{ "prop_type" };
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
				else LOG_WARNING("Load material: {},property {} failed!", mat->_name, k);
			}
			else if (cur_type == ShaderPropertyType::Vector)
			{
				Vector4f vec{};
				if (sscanf_s(v.c_str(), "%f,%f,%f,%f", &vec.r, &vec.g, &vec.b, &vec.a) == 4)
					mat->SetVector(k, vec);
				else LOG_WARNING("Load material: {},property {} failed!", mat->_name, k);
			}
			else if (cur_type == ShaderPropertyType::Uint)
			{
				uint32_t u;
				if (sscanf_s(v.c_str(), "%u", &u) == 1)
					mat->SetUint(k, u);
				else LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
			}
			else if (cur_type == ShaderPropertyType::Texture2D)
			{
				if (v.empty() || v == "null guid") continue;
				auto asset_path = g_pResourceMgr->GuidToAssetPath(Guid(v));
				if (!asset_path.empty())
				{
					mat->SetTexture(k, Load<Texture2D>(asset_path));
				}
				else
				{
					LOG_WARNING("Load material: {}, property {} failed!", mat->_name, k);
				}
			}
		}
		if (is_standard_mat)
		{
			auto standard_mat = static_cast<StandardMaterial*>(mat.get());
			standard_mat->SurfaceType((ESurfaceType::ESurfaceType)standard_mat->GetUint("_surface"));
		}
		auto asset = MakeScope<Asset>();
		asset->_asset_path = asset_path;
		asset->_asset_type = EAssetType::kMaterial;
		asset->_p_obj = mat;
		return asset;
	}

	Scope<Asset> ResourceMgr::LoadMesh(const WString& asset_path)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		WString data;
		Ref<Mesh> mesh;
		if (FileManager::ReadFile(sys_path, data))
		{
			auto c = StringUtils::Split(data, L"\n");
			WString file = c[3].substr(c[3].find_first_of(L":") + 2);
			String innear_file_name = ToChar(c[4].substr(c[4].find_first_of(L":") + 2));
			auto mesh_list = std::move(LoadExternalMesh(file));
			for (auto it = mesh_list.begin(); it != mesh_list.end(); ++it)
			{
				if (it->get()->Name() == innear_file_name)
				{
					auto asset = MakeScope<Asset>();
					asset->_asset_path = asset_path;
					asset->_asset_type = EAssetType::kMesh;
					asset->_external_asset_path = file;
					asset->_p_obj = *it;
					return asset;
				}
			}
		}
		return nullptr;
	}

	Scope<Asset> ResourceMgr::LoadComputeShader(const WString& asset_path)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
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

	Scope<Asset> ResourceMgr::LoadScene(const WString& asset_path)
	{
		using namespace std;
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		String scene_data;
		if (FileManager::ReadFile(sys_path, scene_data))
		{
			Queue<std::tuple<String, String>> lines{};
			int line_count = 0;
			auto scene_data_lines = su::Split(scene_data, "\n");
			for (auto& line : scene_data_lines)
			{
				std::istringstream iss(line);
				std::string key;
				std::string value;
				if (std::getline(iss, key, ':') && std::getline(iss, value))
				{
					key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) {return !std::isspace(ch); }));
					value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](int ch) {return !std::isspace(ch); }));
					lines.push(std::make_pair(key, value));
					++line_count;
				}
			}
			String name_str{ "name" };
			while (std::get<0>(lines.front()) != name_str)
			{
				lines.pop();
			}
			String scene_name = std::get<1>(lines.front());
			lines.pop();
			lines.pop();
			//这里到达场景根节点
			auto loaded_scene = SceneMgr::Create(ToWStr(scene_name.c_str()));
			SceneActor* scene_root = Deserialize<SceneActor>(lines);
			loaded_scene->Root(scene_root);
			loaded_scene->MarkDirty();
			loaded_scene->GetAllActor();
			auto asset = MakeScope<Asset>();
			asset->_asset_path = asset_path;
			asset->_asset_type = EAssetType::kScene;
			asset->_p_obj = loaded_scene;
			return asset;
		}
		else
		{
			g_pLogMgr->LogErrorFormat(L"Load scene failed at path {}", asset_path);
		}
		return nullptr;
	}

	Asset* ResourceMgr::CreateAsset(const WString& asset_path, Ref<Object> obj, bool overwrite)
	{
		bool is_already_exist = _asset_looktable.contains(asset_path);
		if (is_already_exist && !overwrite)
		{
			g_pLogMgr->LogWarningFormat(L"CreateAsset: Asset: {} already exist!", asset_path);
		}
		Guid new_guid = Guid::Generate();
		Scope<Asset> new_asset = nullptr;;
		while (_asset_db.contains(new_guid))
		{
			new_guid = Guid::Generate();
		}
		if (dynamic_cast<Material*>(obj.get()))
		{
			new_asset = MakeScope<Asset>(new_guid, EAssetType::kMaterial, asset_path);
		}
		else if (dynamic_cast<Texture2D*>(obj.get()))
		{
			new_asset = MakeScope<Asset>(new_guid, EAssetType::kTexture2D, asset_path);
		}
		else if (dynamic_cast<Mesh*>(obj.get()))
		{
			new_asset = MakeScope<Asset>(new_guid, EAssetType::kMesh, asset_path);
			new_asset->_addi_info = std::format(L"_{}", ToWStr(obj->Name().c_str()));
		}
		else if (dynamic_cast<Shader*>(obj.get()))
		{
			Shader* shader = dynamic_cast<Shader*>(obj.get());
			auto [vs, ps] = shader->GetShaderEntry();
			new_asset = MakeScope<Asset>(new_guid, EAssetType::kShader, asset_path);
			new_asset->_addi_info = std::format(L"_{}_{}", ToWStr(vs.c_str()), ToWStr(ps.c_str()));
		}
		else if (dynamic_cast<Scene*>(obj.get()))
		{
			Scene* scene = dynamic_cast<Scene*>(obj.get());
			new_asset = MakeScope<Asset>(new_guid, EAssetType::kScene, asset_path);
		}
		AL_ASSERT(new_asset == nullptr);
		if (obj)
		{
			new_asset->_p_obj = obj;
			_object_to_asset.emplace(obj->ID(), new_asset.get());
		}
		RegisterResource(asset_path, obj);
		s_pending_save_assets.emplace(RegisterAsset(std::move(new_asset)));
		return s_pending_save_assets.back();
	}

	void ResourceMgr::DeleteAsset(Asset* asset)
	{
		if (ExistInAssetDB(asset))
			_pending_delete_assets.push(asset);
	}

	bool ResourceMgr::ExistInAssetDB(const Asset* asset)
	{
		return _asset_db.contains(asset->GetGuid());
	}

	bool ResourceMgr::ExistInAssetDB(const WString& asset_path)
	{
		return _asset_looktable.contains(asset_path);
	}

	const WString& ResourceMgr::GetAssetPath(Object* obj) const
	{
		if (_object_to_asset.contains(obj->ID()))
		{
			return _object_to_asset.at(obj->ID())->_asset_path;
		}
		return EmptyWString;
	}

	const Guid& ResourceMgr::GetAssetGuid(Object* obj) const
	{
		if (_object_to_asset.contains(obj->ID()))
		{
			return _object_to_asset.at(obj->ID())->GetGuid();
		}
		return Guid::EmptyGuid();
	}

	const WString& ResourceMgr::GuidToAssetPath(const Guid& guid) const
	{
		if (_asset_db.contains(guid))
		{
			return _asset_db.at(guid)->_asset_path;
		}
		return EmptyWString;
	}

	Asset* ResourceMgr::GetAsset(const WString& asset_path) const
	{
		if (_asset_looktable.contains(asset_path))
		{
			return _asset_db.contains(_asset_looktable.at(asset_path)) ? _asset_db.at(_asset_looktable.at(asset_path)).get() : nullptr;
		}
		return nullptr;
	}

	Vector<Asset*> ResourceMgr::GetAssets(const ISearchFilter& filter) const
	{
		Vector<Asset*> assets;
		for (auto& [guid, asset] : _asset_db)
		{
			if (filter.Filter(asset.get()))
				assets.emplace_back(asset.get());
		}
		return assets;
	}

	bool ResourceMgr::RenameAsset(Asset* p_asset, const WString& new_name)
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
        p_asset->_p_obj->Name(ToChar(new_name));
		return true;
	}

	bool ResourceMgr::MoveAsset(Asset* p_asset, const WString& new_asset_path)
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

	void ResourceMgr::RemoveFromAssetDB(const Asset* asset)
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
		for (auto& [guid, asset] : _asset_db)
		{
			if (cur_count != db_size)
				file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type) << std::endl;
			else
				file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type);
			++cur_count;
			//if (asset->_p_obj)
			//{
			//	if (cur_count != db_size)
			//		file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type) << std::endl;
			//	else
			//		file << ToWChar(guid.ToString()) << "," << asset->_asset_path << "," << EAssetType::ToString(asset->_asset_type);
			//	++cur_count;
			//}
			//else
			//{
			//	g_pLogMgr->LogWarningFormat(L"SaveAssetDB: skip save asset {},because linked obj is null!", asset->_asset_path);
			//}
		}
		_asset_db.clear();
	}

	Asset* ResourceMgr::RegisterAsset(Scope<Asset>&& asset, bool override)
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

	void ResourceMgr::UnRegisterAsset(Asset* asset)
	{
		std::lock_guard<std::mutex> lock(_asset_db_mutex);
		if (ExistInAssetDB(asset->_asset_path))
		{
            if(asset->_p_obj)
			    _object_to_asset.erase(asset->_p_obj->ID());
			_asset_looktable.erase(asset->_asset_path);
			_asset_db.erase(asset->GetGuid());
		}
	}

	void ResourceMgr::RegisterResource(const WString& asset_path, Ref<Object> obj, bool override)
	{
		std::lock_guard<std::mutex> lock(_asset_db_mutex);
		bool exist = _global_resources.contains(asset_path);
		if (exist && override || !exist)
		{
			_global_resources[asset_path] = obj;
			_lut_global_resources[obj->ID()] = _global_resources.find(asset_path);
			_lut_global_resources_by_type[GetObjectAssetType(obj.get())].push_back(_global_resources.find(asset_path));
		}
		else
		{
			g_pLogMgr->LogWarningFormat("RegisterResource: skip register {}", obj->Name());
		}
	}

	void ResourceMgr::UnRegisterResource(const WString& asset_path)
	{
		std::lock_guard<std::mutex> lock(_asset_db_mutex);
		if (_global_resources.contains(asset_path))
		{
			auto& obj = _global_resources[asset_path];
			u32 ref_count = obj.use_count();
			auto& v = _lut_global_resources_by_type[GetObjectAssetType(obj.get())];
			v.erase(std::find_if(v.begin(), v.end(), [&](ResourcePoolContainerIter it)->bool {return it->second.get() == obj.get(); }));
			_lut_global_resources.erase(obj->ID());
			_global_resources.erase(asset_path);
			g_pLogMgr->LogWarningFormat(L"UnRegisterResource: {} ref count is {}", asset_path, ref_count - 1);
		}
	}

	void ResourceMgr::FormatLine(const String& line, String& key, String& value)
	{
		std::istringstream iss(line);
		if (std::getline(iss, key, ':') && std::getline(iss, value))
		{
			key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) {return !std::isspace(ch); }));
			value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](int ch) {return !std::isspace(ch); }));
		}
	}

	void ResourceMgr::ExtractCommonAssetInfo(const WString& asset_path, WString& name, Guid& guid, EAssetType::EAssetType& type)
	{
		auto sys_path = PathUtils::GetResSysPath(asset_path);
		WString data;
		if (FileManager::ReadFile(sys_path, data))
		{
			auto c = StringUtils::Split(data, L"\n");
			guid = Guid(ToChar(c[0].substr(c[0].find_first_of(L":") + 2)));
			name = c[2].substr(c[2].find_first_of(L":") + 2);
			type = EAssetType::FromString(ToChar(c[1].substr(c[1].find_first_of(L":") + 2)));
		}
	}

	EAssetType::EAssetType ResourceMgr::GetObjectAssetType(Object* obj)
	{
		if (dynamic_cast<Material*>(obj))
		{
			return EAssetType::kMaterial;
		}
		else if (dynamic_cast<Texture2D*>(obj))
		{
			return EAssetType::kTexture2D;
		}
		else if (dynamic_cast<Mesh*>(obj))
		{
			return EAssetType::kMesh;
		}
		else if (dynamic_cast<Shader*>(obj))
		{
			return EAssetType::kShader;
		}
		else if (dynamic_cast<ComputeShader*>(obj))
		{
			return EAssetType::kComputeShader;
		}
		return EAssetType::kUndefined;
	}

	bool ResourceMgr::IsFileOnDiskUpdated(const WString& sys_path)
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
		if (newer)
			s_file_last_load_time[sys_path] = fs::file_time_type::clock::now();
		return newer;
	}

	Ref<void> ResourceMgr::ImportResource(const WString& sys_path, const ImportSetting& setting)
	{
		return ImportResourceImpl(sys_path, &setting);
	}

	Ref<void> ResourceMgr::ImportResourceAsync(const WString& sys_path, const ImportSetting& setting, OnResourceTaskCompleted callback)
	{
		s_task_queue.emplace_back([=]() {
			callback(ImportResourceImpl(sys_path, &setting));
			return nullptr;
			});
		return nullptr;
	}

	void ResourceMgr::AddResourceTask(ResourceTask task)
	{
		s_task_queue.emplace_back(task);
	}

	void ResourceMgr::WatchDirectory()
	{
		namespace fs = std::filesystem;
		fs::path dir(kEngineResRootPath + EnginePath::kEngineShaderPath);
		std::chrono::duration<int, std::milli> sleep_duration(1000); // 1秒
		std::unordered_map<fs::path, fs::file_time_type> files;
		static auto reload_shader = [&](const fs::path& file) {
			const WString cur_path = PathUtils::FormatFilePath(file.wstring());
			for (auto it = ResourceBegin<Shader>(); it != ResourceEnd<Shader>(); it++)
			{
				auto shader = IterToRefPtr<Shader>(it);
				if (shader)
				{
					bool match_file = false;
					for (int i = 0; i < shader->PassCount(); i++)
					{
						if (match_file)
							break;
						const auto& pass = shader->GetPassInfo(i);
						for (auto& head_file : pass._source_files)
						{
							if (head_file == cur_path)
							{
								match_file = true;
								_shader_waiting_for_compile.push(shader.get());
								break;
							}
						}
					}

				}
			}
			auto cs = ComputeShader::Get(PathUtils::ExtractAssetPath(cur_path));
			if (cs)
			{
				_compute_shader_waiting_for_compile.push(cs.get());
			}
			};
		bool is_first_execute = true;
		static std::set<fs::path> path_set{};
		static std::unordered_map<fs::path, fs::file_time_type> current_files;
		while (_is_watching_directory)
		{
			TraverseDirectory(dir, path_set);
			for (auto& cur_path : path_set)
			{
				current_files[cur_path] = fs::last_write_time(cur_path);
			}
			for (const auto& [file, last_write_time] : current_files)
			{
				if (files.contains(file))
				{
					if (files[file] != last_write_time)
						reload_shader(file);
				}
				else if (!is_first_execute)
					reload_shader(file);
				files[file] = last_write_time;
			}
			std::this_thread::sleep_for(sleep_duration);
			is_first_execute = false;
		}
	}

	void ResourceMgr::SubmitResourceTask()
	{
		for (auto& task : s_task_queue)
		{
			g_pThreadTool->Enqueue(std::move(task));
		}
		s_task_queue.clear();
	}

	Ref<void> ResourceMgr::ImportResourceImpl(const WString& sys_path, const ImportSetting* setting)
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
		ImportInfo info;
		info._is_succeed = false;
		info._last_write_time = fs::last_write_time(p);
		info._msg = std::format("Import {}...", p.filename().string());
		info._progress = 0.0f;
		info._sys_path = new_sys_path;
		{
			std::lock_guard<std::mutex> lock(_import_infos_mutex);
			_import_infos.push_back(info);
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
			auto mesh_list = std::move(LoadExternalMesh(external_asset_path));
			if (!mesh_list.empty())
			{
				auto mesh_import_setting = dynamic_cast<const MeshImportSetting*>(setting);
				mesh_import_setting = mesh_import_setting ? mesh_import_setting : &MeshImportSetting::Default();
				for (auto& mesh : mesh_list)
				{
					WString imported_asset_path = created_asset_dir;
					imported_asset_path.append(std::format(L"{}.alasset", ToWStr(mesh->Name().c_str())));
					loaded_objects.push(std::make_tuple(imported_asset_path, mesh));
					if (mesh_import_setting->_is_import_material)
					{
						for (auto it = mesh->GetCacheMaterials().begin(); it != mesh->GetCacheMaterials().end(); it++)
						{
							auto mat = MakeRef<StandardMaterial>(it->_name);
							if (!it->_textures[0].empty())
							{
								auto albedo = ImportResource(ToWChar(it->_textures[0]));
								if (albedo != nullptr)
									mat->SetTexture(StandardMaterial::StandardPropertyName::kAlbedo._tex_name, std::static_pointer_cast<Texture>(albedo).get());
							}
							if (!it->_textures[1].empty())
							{
								auto normal = ImportResource(ToWChar(it->_textures[1]));
								if (normal != nullptr)
									mat->SetTexture(StandardMaterial::StandardPropertyName::kNormal._tex_name, std::static_pointer_cast<Texture>(normal).get());
							}
							imported_asset_path = created_asset_dir;
							imported_asset_path.append(std::format(L"{}.alasset", ToWStr(it->_name.c_str())));
							loaded_objects.push(std::make_tuple(imported_asset_path, mat));
						}
					}
				}
				ret_res = std::static_pointer_cast<void>(mesh_list.front());
			}
		}
		else if (kHDRImageExt.contains(ext) || kLDRImageExt.contains(ext))
		{
			asset_type = EAssetType::kTexture2D;
			auto tex = LoadExternalTexture(external_asset_path, setting);
			WString imported_asset_path = created_asset_dir;
			imported_asset_path.append(std::format(L"{}.alasset", ToWStr(tex->Name().c_str())));
			loaded_objects.push(std::make_tuple(imported_asset_path, tex));
		}
		else {}
		g_pLogMgr->LogFormat(L"Import asset: {} succeed,cost {}ms", new_sys_path, time_mgr.GetElapsedSinceLastMark());
		{
			std::lock_guard<std::mutex> lock(_import_infos_mutex);
			_import_infos.erase(std::remove_if(_import_infos.begin(), _import_infos.end(), [&](auto it) {return it._msg == info._msg; }), _import_infos.end());
		}
		while (!loaded_objects.empty())
		{
			auto& [path, obj] = loaded_objects.front();
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
		for (auto& f : _asset_changed_callbacks)
		{
			f();
		}
	}
}
