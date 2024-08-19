#include "Objects/SceneActor.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/SceneMgr.h"
#include "Objects/CameraComponent.h"
#include "Objects/LightComponent.h"
#include "Objects/StaticMeshComponent.h"
#include "pch.h"
#include <Render/Gizmo.h>

namespace Ailu
{
	SceneActor::SceneActor()
	{
		_p_transform = AddComponent<TransformComponent>();
        _base_aabb = AABB(Vector3f(-kBasAABBInitialSize),Vector3f(kBasAABBInitialSize));
	}
	SceneActor::~SceneActor()
	{

	}
	void SceneActor::Tick(const float& delta_time)
	{
		Actor::Tick(delta_time);
        const static AABB s_base_aabb = AABB(Vector3f(-kBasAABBInitialSize),Vector3f(kBasAABBInitialSize));
        Vector3f world_pos = _p_transform->GetPosition();
        f32 scale = kBasAABBInitialSize;
        f32 dis = Distance(Camera::sCurrent->Position(),world_pos);
        f32 scale_factor = std::max<f32>(dis * 0.00025f,1.0f);
        scale *= scale_factor;
        _base_aabb._min = -scale + world_pos;
        _base_aabb._max = scale + world_pos;
		OnGizmo();
	}
	void SceneActor::OnGizmo()
	{
		if (this == g_pSceneMgr->_p_selected_actor)
		{
            //Gizmo::DrawAABB(_base_aabb,Colors::kWhite);
			for (auto& comp : _components)
			{
				if (comp->GetType() == EComponentType::kCameraComponent)
				{
					Camera::sSelected = &static_cast<CameraComponent*>(comp.get())->_camera;
				}
				else
				{
					Camera::sSelected = nullptr;
				}
				comp->OnGizmo();
			}
		}
	}
	void SceneActor::Serialize(std::ostream& os, String indent)
	{
		Actor::Serialize(os, indent);
	}
	void* SceneActor::DeserializeImpl(Queue<std::tuple<String, String>>& formated_str)
	{
		auto [k, v] = formated_str.front();
		if (k != "Name")
		{
			g_pLogMgr->LogError("Deserialize actor failed,first line must begin with [Name]");
			return nullptr;
		}
		SceneActor* actor = Actor::Create<SceneActor>();
		actor->RemoveAllComponent();
		actor->Name(v);
		formated_str.pop();
		k = TP_ZERO(formated_str.front());
		if (k == "Components")
		{
			formated_str.pop();
			auto comp_type = TP_ZERO(formated_str.front());
			while (comp_type != "Children")
			{
				if (comp_type == Component::GetTypeName(TransformComponent::GetStaticType()))
					actor->AddComponent(Deserialize<TransformComponent>(formated_str));
				else if (comp_type == Component::GetTypeName(StaticMeshComponent::GetStaticType()))
					actor->AddComponent(Deserialize<StaticMeshComponent>(formated_str));
				else if (comp_type == Component::GetTypeName(LightComponent::GetStaticType()))
					actor->AddComponent(Deserialize<LightComponent>(formated_str));
				else if (comp_type == Component::GetTypeName(CameraComponent::GetStaticType()))
					actor->AddComponent(Deserialize<CameraComponent>(formated_str));
				else if (comp_type == Component::GetTypeName(SkinedMeshComponent::GetStaticType()))
					actor->AddComponent(Deserialize<SkinedMeshComponent>(formated_str));
				else
				{
					AL_ASSERT_MSG(true, "Unhandled component type");
					g_pLogMgr->LogWarningFormat("Unhandled component type {}", comp_type);
				}
				comp_type = TP_ZERO(formated_str.front());
			}
		}
		k = TP_ZERO(formated_str.front());
		if (k == "Children")
		{
			int child_num = static_cast<int>(LoadFloat(TP_ONE(formated_str.front()).c_str()));
			formated_str.pop();
			for (size_t i = 0; i < child_num; i++)
			{
				auto child = static_cast<Actor*>(Deserialize<SceneActor>(formated_str));
				child->Parent(actor);
				actor->AddChild(child);
			}
		}
		actor->_p_transform = actor->GetComponent<TransformComponent>();
		return actor;
	}
}
