#include "Framework/Script/ScriptScene.h"

#include "Assets/PrefabAsset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Scene/Component.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace
    {
        ScriptEntity MakeScriptEntity(SceneManagement::Scene *scene, ECS::Entity entity)
        {
            ScriptEntity result;
            result._scene = scene;
            result._entity = entity;
            return result;
        }
    }

    ScriptScene ScriptScene::CurrentScene()
    {
        auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
        ScriptScene result;
        result._scene = scene;
        return result;
    }
    bool ScriptScene::IsValid() const { return _scene != nullptr; }
    ScriptEntity ScriptScene::FindEntity(const String &guid) const
    {
        return IsValid() ? MakeScriptEntity(_scene, _scene->FindEntity(Guid(guid))) : ScriptEntity{};
    }
    ScriptEntity ScriptScene::FindEntityByName(const String &name) const
    {
        if (!IsValid()) return {};
        u32 index = 0u;
        for (const auto &tag : _scene->GetRegister().View<ECS::TagComponent>())
        {
            const ECS::Entity entity = _scene->GetRegister().GetEntity<ECS::TagComponent>(index++);
            if (tag._name == name) return MakeScriptEntity(_scene, entity);
        }
        return MakeScriptEntity(_scene, ECS::kInvalidEntity);
    }
    ScriptEntity ScriptScene::Find(const String &name) const { return FindEntityByName(name); }
    ScriptEntity ScriptScene::FindGuid(const String &guid) const { return FindEntity(guid); }
    ScriptEntity ScriptScene::CreateEntity(const String &name) const
    {
        return IsValid() ? MakeScriptEntity(_scene, _scene->AddObject(name)) : ScriptEntity{};
    }
    ScriptEntity ScriptScene::CreateEntity(const ScriptAssetValue &prefab) const
    {
        if (!IsValid() || prefab._guid.IsEmpty())
            return {};

        ResourceMgr::Get().Load<PrefabAssetDocument>(prefab._guid);
        const Ref<PrefabAssetDocument> prefab_asset = ResourceMgr::Get().GetRef<PrefabAssetDocument>(prefab._guid);
        if (prefab_asset == nullptr)
            return {};

        const SceneManagement::PrefabInstantiateResult result = SceneManagement::PrefabSystem::Instantiate(*_scene, *prefab_asset);
        return result._root == ECS::kInvalidEntity ? ScriptEntity{} : MakeScriptEntity(_scene, result._root);
    }
    bool ScriptScene::DestroyEntity(const ScriptEntity &entity) const
    {
        if (!IsValid() || entity._scene != _scene || !_scene->IsValidEntity(entity._entity))
            return false;
        _scene->RemoveObject(entity._entity);
        return true;
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
                _scene->GetRegister().IsComponentEnabled<ECS::CCamera>(entity))
            {
                ScriptCamera result;
                result._scene = _scene;
                result._entity = entity;
                return result;
            }
        }
        return {};
    }
}
