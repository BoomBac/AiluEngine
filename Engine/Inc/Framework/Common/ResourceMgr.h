#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/SceneMgr.h"
#include "Render/Material.h"

namespace Ailu
{
	struct EngineResourceType
	{
		inline const static std::string Material = "material";
		inline const static std::string Mesh = "mesh";
		inline const static std::string Texture2D = "texture2d";
	};

	class ResourceMgr : public IRuntimeModule
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);
		void SaveMaterial(Material* mat,std::string path);
		Ref<Material> LoadMaterial(std::string path);
	private:
		void SaveSceneImpl(Scene* scene, std::string& scene_path);
	};
	extern ResourceMgr* g_pResourceMgr;
}

#endif // !RESOURCE_MGR_H__
