#include "Framework/Script/ScriptScene.h"

#include "Assets/PrefabAsset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"

namespace Ailu
{
    ScriptScene ScriptScene::CurrentScene()
    {
        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        return {scene};
    }
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
    ScriptEntity ScriptScene::Find(const String &name) const { return FindEntityByName(name); }
    ScriptEntity ScriptScene::FindGuid(const String &guid) const { return FindEntity(guid); }
    ScriptEntity ScriptScene::CreateEntity(const String &name) const { return IsValid() ? ScriptEntity{_scene, _scene->AddObject(name)} : ScriptEntity{}; }
    ScriptEntity ScriptScene::CreateEntity(const ScriptAssetValue &prefab) const
    {
        if (!IsValid() || prefab._guid.IsEmpty())
            return {};

        ResourceMgr::Get().Load<PrefabAssetDocument>(prefab._guid);
        const Ref<PrefabAssetDocument> prefab_asset = ResourceMgr::Get().GetRef<PrefabAssetDocument>(prefab._guid);
        if (prefab_asset == nullptr)
            return {};

        const SceneManagement::PrefabInstantiateResult result = SceneManagement::PrefabSystem::Instantiate(*_scene, *prefab_asset);
        return result._root == ECS::kInvalidEntity ? ScriptEntity{} : ScriptEntity{_scene, result._root};
    }
    ScriptEntity ScriptScene::Spawn(const ScriptAssetValue &prefab) const { return CreateEntity(prefab); }
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
