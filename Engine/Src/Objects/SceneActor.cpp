#include "pch.h"
#include "Objects/SceneActor.h"
#include "Framework/Common/LogMgr.h"
#include "Objects/LightComponent.h"
#include "Objects/StaticMeshComponent.h"

namespace Ailu
{
	SceneActor::SceneActor()
	{
		_p_transform = AddComponent<TransformComponent>();
	}
	SceneActor::~SceneActor()
	{

	}
	Transform& SceneActor::GetTransform()
	{
		return _p_transform->_transform;
	}
	void SceneActor::Serialize(std::ofstream& file, String indent)
	{
	}
	void SceneActor::Serialize(std::ostream& os, String indent)
	{
		Actor::Serialize(os,indent);
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
				if (comp_type == TransformComponent::GetStaticType())
					actor->AddComponent(Deserialize<TransformComponent>(formated_str));
				else if (comp_type == StaticMeshComponent::GetStaticType())
					actor->AddComponent(Deserialize<StaticMeshComponent>(formated_str));
				else if (comp_type == LightComponent::GetStaticType())
					actor->AddComponent(Deserialize<LightComponent>(formated_str));
				else
				{
					g_pLogMgr->LogWarningFormat("Unhandled component type {}",comp_type);
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
