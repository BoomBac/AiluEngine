#include "pch.h"
#include "Framework/Common/SceneMgr.h"
#include "Objects/CommonActor.h"
#include "Objects/StaticMeshComponent.h"
#include "Objects/CameraComponent.h"
#include "Framework/Common/LogMgr.h"
#include "Render/Camera.h"
#include "Render/RenderQueue.h"
#include "Framework/Common/Path.h"
#include "Framework/Common/Profiler.h"


namespace Ailu
{
	static String GetIndentation(int level)
	{
		String ret{ "" };
		while (level--)
		{
			ret.append("  ");
		}
		return ret;
	}

	static String GetSceneSysPath(const String& scene_path)
	{
		return kEngineRootPath + scene_path;
	}

	inline static void ProcessValueAndPop(Queue<std::tuple<String, String>>& scene_data, std::function<void(String)> func)
	{
		func(std::get<1>(scene_data.front()));
		scene_data.pop();
	}

	static void LoadCamera(Queue<std::tuple<String, String>>& scene_data, Camera& out_camera)
	{
		while (std::get<0>(scene_data.front()) != "Type") scene_data.pop();
		out_camera.Type(std::get<1>(scene_data.front()) == ECameraType::ToString(ECameraType::kPerspective) ? ECameraType::kPerspective : ECameraType::kOrthographic);
		scene_data.pop();
		Vector3f v3{};
		Vector4f v4{};
		ProcessValueAndPop(scene_data, [&](String str) {LoadVector(str.c_str(), v3); out_camera.Position(v3); });
		ProcessValueAndPop(scene_data, [&](String str) {LoadVector(str.c_str(), v4); out_camera.Rotation(v4); });
		ProcessValueAndPop(scene_data, [&](String str) {out_camera.FovH(LoadFloat(str.c_str())); });
		ProcessValueAndPop(scene_data, [&](String str) {out_camera.Aspect(LoadFloat(str.c_str())); });
		ProcessValueAndPop(scene_data, [&](String str) {out_camera.Near(LoadFloat(str.c_str())); });
		ProcessValueAndPop(scene_data, [&](String str) {out_camera.Far(LoadFloat(str.c_str())); });
		Vector2f v2{};
		ProcessValueAndPop(scene_data, [&](String str) {LoadVector(str.c_str(), v2); FirstPersonCameraController::s_instance._rotation = v2; });
		out_camera.RecalculateMarix(true);
	}

	int SceneMgr::Initialize()
	{
		g_pLogMgr->LogFormat("Begin load scene from {}...", GetSceneSysPath("Scenes/default.almap").c_str());
		g_pSceneMgr->_p_current = LoadScene("default.almap");
		//g_pSceneMgr->_p_current = Scene::GetDefaultScene();
		Camera::sCurrent = _p_current->GetActiveCamera();
		g_pLogMgr->Log("SceneMgr end");
		return 0;
	}
	void SceneMgr::Finalize()
	{
		SaveScene(_p_current, "default.almap");
	}

	void SceneMgr::Tick(const float& delta_time)
	{
		if (_p_current)
		{
			while (!_pending_delete_actors.empty())
			{
				auto actor = _pending_delete_actors.front();
				_pending_delete_actors.pop();
				_p_current->RemoveObject(actor);
			}
			for (auto& actor : _p_current->GetAllActor())
			{
				actor->Tick(delta_time);
			}
			{
				CPUProfileBlock b("CameraCull");
				Cull(Camera::sCurrent, _p_current);
			}
		}
	}

	Scene* SceneMgr::Create(const std::string& name)
	{
		s_all_scene.push_back(std::move(MakeScope<Scene>(name)));
		return s_all_scene.back().get();
	}

	SceneActor* SceneMgr::AddSceneActor(std::string_view name, std::string_view mesh)
	{
		auto p_actor = Actor::Create<SceneActor>(name.data());
		p_actor->AddComponent<StaticMeshComponent>(MeshPool::GetMesh(mesh.data()), MaterialLibrary::GetMaterial("Materials/StandardPBR_new.alasset"));
		_p_current->AddObject(p_actor);
		return p_actor;
	}

	SceneActor* SceneMgr::AddSceneActor(std::string_view name, const Camera& camera)
	{
		auto p_actor = Actor::Create<SceneActor>(name.data());
		p_actor->AddComponent<CameraComponent>(camera);
		_p_current->AddObject(p_actor);
		return p_actor;
	}

	SceneActor* SceneMgr::AddSceneActor()
	{
		auto p_actor = Actor::Create<SceneActor>();
		_p_current->AddObject(p_actor);
		return p_actor;
	}

	void SceneMgr::DeleteSceneActor(SceneActor* actor)
	{
		_pending_delete_actors.push(actor);
	}

	void SceneMgr::SaveScene(Scene* scene, const String& scene_path)
	{
		using namespace std;
		std::ostringstream ss;
		try
		{
			String level1 = GetIndentation(1), level2 = GetIndentation(2), level3 = GetIndentation(3), level4 = GetIndentation(4);
			ss << "SceneName: " << scene->Name() << endl;
			ss << "SceneCamera: " << "scene_camera" << endl;
			ss << level1 << "Type: " << ECameraType::ToString(Camera::sCurrent->Type()) << endl;
			ss << level1 << "Position: " << Camera::sCurrent->Position() << endl;
			ss << level1 << "Rotation: " << Camera::sCurrent->Rotation() << endl;
			ss << level1 << "Fov: " << Camera::sCurrent->FovH() << endl;
			ss << level1 << "Aspect: " << Camera::sCurrent->Aspect() << endl;
			ss << level1 << "Near: " << Camera::sCurrent->Near() << endl;
			ss << level1 << "Far: " << Camera::sCurrent->Far() << endl;
			ss << level1 << "ControllerRotation: " << FirstPersonCameraController::s_instance._rotation << endl;
			ss << "SceneGraph: " << endl;
			//SerializeActor(ss, scene->GetSceneRoot(), 2);
			scene->Root()->Serialize(ss, level1);
		}
		catch (const std::exception&)
		{
			g_pLogMgr->LogErrorFormat("Serialize failed when save scene: {}!", scene->Name());
			return;
		}
		auto sys_path = GetSceneSysPath(scene_path);
		ofstream file(sys_path, ios_base::out);
		if (!file.is_open())
		{
			g_pLogMgr->LogErrorFormat("Save scene: {} to {} failed with file open failed", scene->Name(), sys_path);
			return;
		}
		String serialized_str = ss.str();
		std::istringstream iss(serialized_str);
		std::string line;
		while (std::getline(iss, line))
		{
			file << line << endl;
		}
		g_pLogMgr->LogFormat("Save scene: {} to {};", scene->Name(), sys_path);
		file.close();
	}

	Scene* SceneMgr::LoadScene(const String& scene_path)
	{
		using namespace std;
		auto sys_path = GetSceneSysPath(scene_path);
		std::ifstream scene_data(sys_path);
		if (!scene_data.is_open())
		{
			g_pLogMgr->LogErrorFormat("Load scene at path {} failed!", sys_path);
			return nullptr;
		}
		String line{};
		Queue<std::tuple<String, String>> lines{};
		int line_count = 0;
		while (std::getline(scene_data, line))
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
		scene_data.close();
		String scene_name = std::get<1>(lines.front());
		lines.pop();
		lines.pop();
		Scene* loaded_scene = SceneMgr::Create(scene_name);
		LoadCamera(lines, *loaded_scene->GetActiveCamera());
		lines.pop();
		SceneActor* scene_root = Deserialize<SceneActor>(lines);
		loaded_scene->Root(scene_root);
		loaded_scene->MarkDirty();
		loaded_scene->GetAllActor();//将根节点的子节点信息填充至Scene的actor容器
		return loaded_scene;
	}

	void SceneMgr::Cull(Camera* cam, Scene* p_scene)
	{
		RenderQueue::ClearQueue();
		auto& cam_vf = cam->GetViewFrustum();
		for (auto static_mesh : p_scene->GetAllStaticRenderable())
		{
			if (static_mesh->GetMesh())
			{
				auto& aabb = static_mesh->GetAABB();
				auto submesh_count = static_mesh->GetMesh()->SubmeshCount();
				auto materials = static_mesh->GetMaterials();
				auto replace_mat = materials.empty() ? nullptr : materials[0].get();
				if (ViewFrustum::Conatin(cam_vf, aabb))
				{
					for (int i = 0; i < submesh_count; i++)
					{
						RenderQueue::Enqueue(RenderQueue::kQpaque, static_mesh->GetMesh().get(), i < materials.size() ? materials[i].get() : replace_mat,
							static_mesh->GetOwner()->GetComponent<TransformComponent>()->GetMatrix(), i, 1);
					}
				}

			}
		}
	}

	//--------------------------------------------------------Scene begin-----------------------------------------------------------------------

	Scene::Scene(const std::string& name)
	{
		//_p_root = Actor::Create<SceneActor>(name);
		_name = name;
		_all_static_renderalbes.resize(kMaxStaticRenderableNum);
		FillActorList = [this](SceneActor* actor) {
			_all_objects.emplace_back(actor);
			auto light = actor->GetComponent<LightComponent>();
			if (light != nullptr)
			{
				_all_lights.emplace_back(light);
			}
			auto static_mesh = actor->GetComponent<StaticMeshComponent>();
			if (static_mesh != nullptr && static_mesh->Active())
			{
				_all_static_renderalbes.emplace_back(static_mesh);
			}
			};
	}

	void Scene::AddObject(SceneActor* actor)
	{
		auto p = static_cast<Actor*>(actor);
		_p_root->AddChild(p);
		_b_dirty = true;
	}

	void Scene::RemoveObject(SceneActor* actor)
	{
		auto p = static_cast<Actor*>(actor);
		_p_root->RemoveChild(p);
		_b_dirty = true;
	}


	std::list<SceneActor*>& Scene::GetAllActor()
	{
		if (_b_dirty)
		{
			_all_objects.clear();
			_all_lights.clear();
			_all_static_renderalbes.clear();
			TravelAllActor(_p_root, FillActorList);
			_b_dirty = false;
		}
		return _all_objects;
	}

	SceneActor* Scene::GetSceneActorByID(const u32& id)
	{
		for (auto& actor : _all_objects)
		{
			if (actor->Id() == id) return actor;
		}
		LOG_WARNING("Can't find actor with id: {} in scene {},will return first actor", id, _name);
		return _all_objects.front();
	}

	SceneActor* Scene::GetSceneActorByIndex(const u32& index)
	{
		if (index >= _all_objects.size())
		{
			return _all_objects.front();
			LOG_WARNING("Can't find actor with index: {} in scene {},will return first actor", index, _name);
		}
		else
		{
			int cur_index = 0;
			for (auto it = _all_objects.begin(); it != _all_objects.end(); it++)
			{
				if (cur_index == index) return *it;
				else ++cur_index;
			}
		}
		return _all_objects.front();
	}

	void Scene::MarkDirty()
	{
		_b_dirty = true;
	}

	Camera* Scene::GetActiveCamera()
	{
		return &_scene_cam;
	}

	Scene* Scene::GetDefaultScene()
	{
		auto _p_actor = Actor::Create<SceneActor>("stone");
		_p_actor->AddComponent<StaticMeshComponent>(MeshPool::GetMesh("sphere"), MaterialLibrary::GetMaterial("Materials/StandardPBR_new.alasset"));
		auto _p_light = Actor::Create<LightActor>("directional_light");
		SceneActor* point_light = Actor::Create<LightActor>("point_light");
		point_light->GetComponent<LightComponent>()->_light_type = ELightType::kPoint;
		point_light->GetComponent<LightComponent>()->_light._light_param.x = 500.0f;

		SceneActor* spot_light = Actor::Create<LightActor>("spot_light");
		auto transf_comp = spot_light->GetTransformComponent();
		transf_comp->SetPosition({ 0.0,500.0,0.0 });
		transf_comp->SetRotation(Quaternion());
		auto light_comp = spot_light->GetComponent<LightComponent>();
		light_comp->_light_type = ELightType::kSpot;
		light_comp->_light._light_param.x = 500.0f;
		light_comp->_light._light_param.y = 45.0f;
		light_comp->_light._light_param.z = 60.0f;

		auto scene = SceneMgr::Create("default_scene");
		scene->Root(Actor::Create<SceneActor>("root"));
		scene->AddObject(_p_actor);
		scene->AddObject(_p_light);
		scene->AddObject(point_light);
		scene->AddObject(spot_light);
		scene->_scene_cam = *Camera::GetDefaultCamera();
		return scene;
	}

	std::list<LightComponent*>& Scene::GetAllLight()
	{
		return _all_lights;
	}

	void Scene::TravelAllActor(SceneActor* actor, ActorEvent& e)
	{
		e(actor);
		if (actor->GetChildNum() > 0)
		{
			for (auto& child : actor->GetAllChildren())
			{
				TravelAllActor(static_cast<SceneActor*>(child), e);
			}
		}
	}
}
