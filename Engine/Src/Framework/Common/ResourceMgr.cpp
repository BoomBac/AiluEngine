#include "pch.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Framework/Parser/AssetParser.h"
#include "Framework/Common/TimeMgr.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ThreadPool.h"

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
