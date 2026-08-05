#include "Scene/EntityReference.h"
#include "Scene/Scene.h"
#include "Objects/SerializeSpecializations.h"// 使 SerializerWrapper<Guid> 的显式特化可见，避免隐式实例化主模板造成重复定义

namespace Ailu
{
    ECS::Entity EntityReference::Resolve(const SceneManagement::Scene &scene) const
    {
        if (_entity_guid.IsEmpty())
            return ECS::kInvalidEntity;
        if (_scene_guid.IsValid())
        {
            // 跨场景引用：仅当目标场景可提供自身身份时校验，避免把引用错误解析到另一场景的同名实体。
            const Guid scene_asset_guid = scene.AssetGuid();
            if (scene_asset_guid.IsValid() && !(scene_asset_guid == _scene_guid))
                return ECS::kInvalidEntity;
        }
        return scene.FindEntity(_entity_guid);
    }

    bool EntityReference::IsValid() const
    {
        return _entity_guid.IsValid();
    }

    void SerializerWrapper<EntityReference>::Serialize(void *data, FArchive &ar, const String *name)
    {
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        auto *ref = static_cast<EntityReference *>(data);
        SerializerWrapper<Guid>::Serialize(&ref->_scene_guid, ar, &EntityReference::kSceneGuid);
        SerializerWrapper<Guid>::Serialize(&ref->_entity_guid, ar, &EntityReference::kEntityGuid);
        if (sar && name)
            sar->EndObject();
    }

    void SerializerWrapper<EntityReference>::Deserialize(void *data, FArchive &ar, const String *name)
    {
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        auto *ref = static_cast<EntityReference *>(data);
        SerializerWrapper<Guid>::Deserialize(&ref->_scene_guid, ar, &EntityReference::kSceneGuid);
        SerializerWrapper<Guid>::Deserialize(&ref->_entity_guid, ar, &EntityReference::kEntityGuid);
        if (sar && name)
            sar->EndObject();
    }
}// namespace Ailu
