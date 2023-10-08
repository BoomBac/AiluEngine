#pragma once
#ifndef __RESOURCE_MGR_H__
#define __RESOURCE_MGR_H__
#include "Framework/Interface/IRuntimeModule.h"
#include "Framework/Common/SceneMgr.h"

namespace Ailu
{
	class ResourceMgr : public IRuntimeModule
	{
	public:
		int Initialize() final;
		void Finalize() final;
		void Tick(const float& delta_time) final;
		void SaveScene(Scene* scene, std::string& scene_path);
		Scene* LoadScene(std::string& scene_path);
	private:
		void SaveSceneImpl(Scene* scene, std::string& scene_path);
	};
	extern ResourceMgr* g_pResourceMgr;
}

#endif // !RESOURCE_MGR_H__
