#include "Framework/Script/ScriptScene.h"

#include "Scene/Component.h"
#include "Scene/Scene.h"

namespace Ailu
{
    bool ScriptScene::IsValid() const { return _scene != nullptr; }
    ScriptEntity ScriptScene::FindEntity(const String &guid) const
    {
        return IsValid() ? ScriptEntity{_scene, _scene->FindEntity(Guid(guid))} : ScriptEntity{};
    }
    ScriptEntity ScriptScene::FindEntityByName(const String &name) const
    {
        if (!IsValid()) return {};
        u32 index = 0u;
        for (const auto &tag : _scene->GetRegister().View<ECS::TagComponent>())
        {
            const ECS::Entity entity = _scene->GetRegister().GetEntity<ECS::TagComponent>(index++);
            if (tag._name == name) return {_scene, entity};
        }
        return {_scene, ECS::kInvalidEntity};
    }
    ScriptEntity ScriptScene::CreateEntity(const String &name) const { return IsValid() ? ScriptEntity{_scene, _scene->AddObject(name)} : ScriptEntity{}; }
    ScriptCamera ScriptScene::GetMainCamera() const
    {
        if (!IsValid()) return {};
        u32 index = 0u;
        for (const auto &camera : _scene->GetRegister().View<ECS::CCamera>())
        {
            const ECS::Entity entity = _scene->GetRegister().GetEntity<ECS::CCamera>(index++);
            const auto *tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
            if (tag != nullptr && tag->_tag == "MainCamera" && _scene->IsEntityEnabled(entity) &&
                _scene->GetRegister().IsComponentEnabled<ECS::CCamera>(entity)) return {_scene, entity};
        }
        return {};
    }
}
