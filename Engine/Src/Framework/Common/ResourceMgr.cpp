#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"
#include "GlobalMarco.h"

namespace Ailu
{
	int ResourceMgr::Initialize()
	{
        LOG_WARNING("Begin init engine internal resource...");
        g_pTimeMgr->Mark();
        MaterialPool::CreateMaterial(ShaderLibrary::Add(GetResPath("Shaders/shaders.hlsl")), "StandardPBR");
        MaterialPool::CreateMaterial(ShaderLibrary::Add(GetResPath("Shaders/PureColor.hlsl")), "WireFrame");
        auto parser = TStaticAssetLoader<EResourceType::kStaticMesh, EMeshLoader>::GetParser(EMeshLoader::kFbx);
        //_plane = parser->Parser(GET_RES_PATH(Meshs/plane.fbx));
        //_plane = parser->Parser(GET_RES_PATH(Meshs/gizmo.fbx));

        //_tree = parser->Parser(GetResPath("Meshs/stone.fbx"));
        //_tree = parser->Parser(GetResPath("Meshs/plane.fbx"));
        //_tree = parser->Parser(GetResPath("Meshs/space_ship.fbx"));
        parser->Parser(GetResPath("Meshs/sphere.fbx"));
        auto png_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kPNG);
        auto tga_parser = TStaticAssetLoader<EResourceType::kImage, EImageLoader>::GetParser(EImageLoader::kTGA);

        //_tex_water = tga_parser->Parser(GET_RES_PATH(Textures/PK_stone03_static_0_D.tga));
        //tga_parser->Parser(GetResPath("Textures/PK_stone03_static_0_D.tga"));
        //tga_parser->Parser(GetResPath("Textures/PK_stone03_static_0_N.tga"));
        //png_parser->Parser(GetResPath("Textures/Intergalactic Spaceship_color_4.png"));
        png_parser->Parser(GetResPath("Textures/MyImage01.jpg"));
        //png_parser->Parser(GetResPath("Textures/Intergalactic Spaceship_emi.jpg"));
        //png_parser->Parser(GetResPath("Textures/Intergalactic Spaceship_nmap_2_Tris.jpg"));
        LOG_WARNING("Finish after {}ms",g_pTimeMgr->GetElapsedSinceLastMark());
		return 0;
	}
	void ResourceMgr::Finalize()
	{
        SaveMaterial(MaterialPool::GetMaterial("StandardPBR").get(),std::string(STR2(RES_PATH)) + "Materials/");
	}
	void ResourceMgr::Tick(const float& delta_time)
	{
	}

    void ResourceMgr::SaveScene(Scene* scene, std::string& scene_path)
    {
        g_thread_pool->Enqueue(&ResourceMgr::SaveSceneImpl,this,scene,scene_path);
    }

    Scene* ResourceMgr::LoadScene(std::string& scene_path)
    {
        return nullptr;
    }

    void ResourceMgr::SaveMaterial(Material* mat, std::string path)
    {
        using std::endl;
        auto path_name = path + mat->Name() + ".alasset";
        std::multimap<std::string, SerializableProperty*> props{};
        std::ofstream out_mat(path_name, std::ios::out | std::ios::trunc);
        if (!out_mat.is_open())
        {
            std::cerr << "无法打开文件" << std::endl;
        }
        out_mat << "type : " << "material" << endl;
        out_mat << "shader_path : " << ShaderLibrary::GetShaderPath(mat->_p_shader->GetName()) << endl;
        for (auto& prop : mat->properties)
        {
            props.insert(std::make_pair(prop.second._type_name,&prop.second));
        }
        std::string cur_prop_type = "type";
        for (auto& prop : props)
        {
            if (cur_prop_type != prop.first)
            {
                out_mat << "  prop_type : " << prop.first << endl;
                cur_prop_type = prop.first;
            }
            if (prop.second->_type_name == ShaderPropertyType::Color || prop.second->_type_name == ShaderPropertyType::Vector)
                out_mat << "    " << prop.second->_name << " : " << SerializableProperty::GetProppertyValue<Vector4f>(*prop.second) << endl;
            else if (prop.second->_type_name == ShaderPropertyType::Float)
                out_mat << "    " << prop.second->_name << " : " << SerializableProperty::GetProppertyValue<float>(*prop.second) << endl;
        }
        out_mat.close();
        LOG_WARNING("Save material to {}", path_name);
    }

    Ref<Material> ResourceMgr::LoadMaterial(std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open()) 
        {
            LOG_ERROR("Load material with path: {} failed!",path);
            return nullptr;
        }
        std::vector<std::tuple<std::string, std::string>> lines{};
        std::string line;
        while (std::getline(file, line)) 
        {
            std::istringstream iss(line);
            std::string key;
            std::string value;
            if (std::getline(iss, key, ':') && std::getline(iss, value)) 
            {
                key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](int ch) {return !std::isspace(ch);}));
                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](int ch) {return !std::isspace(ch);}));
                if (value != "Material")
                {
                    LOG_ERROR("Load material with path: {} failed,asset isn't mateiral", path);
                    return nullptr;
                }
                lines.emplace_back(std::make_pair(key,value));
            }
        }
        file.close();
        auto mat = MaterialPool::CreateMaterial(ShaderLibrary::Add(std::get<1>(lines[1])), GetFileName(path));
        std::string cur_type{};
        for (int i = 2; i < lines.size();++i)
        {
            auto& [k, v] = lines[i];
            if (k == "prop_type" && cur_type != v)
            {
                cur_type = v;
                continue;
            }
            if (cur_type == ShaderPropertyType::Color)
            {
                auto prop = mat->properties.find(k);
                Vector4f vec{};
                if (sscanf(v.c_str(), "%f,%f,%f,%f", &vec.r, &vec.g, &vec.b, &vec.a) == 4) memcpy(prop->second._value_ptr, &vec, sizeof(Vector4f));
                else 
            }
        }
    }
    void ResourceMgr::SaveSceneImpl(Scene* scene, std::string& scene_path)
    {
        std::ofstream outputFile(scene_path);
        if (!outputFile.is_open())
        {
            std::cerr << "无法打开文件" << std::endl;
        }
        outputFile << "这是要写入到文件的文本内容." << std::endl;
        outputFile << "可以写入更多内容." << std::endl;
        LOG_WARNING("Save scene to {}",scene_path);
        outputFile.close();
    }
}
