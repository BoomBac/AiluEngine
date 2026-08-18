#pragma once
#ifndef __PREFAB_SYSTEM_H__
#define __PREFAB_SYSTEM_H__

#include "Scene/Scene.h"

namespace Ailu
{
    class PrefabAssetDocument;
}

namespace Ailu::SceneManagement
{
    struct PrefabInstantiateResult
    {
        ECS::Entity _root = ECS::kInvalidEntity;
    };

    class AILU_API PrefabSystem
    {
    public:
        static Ref<PrefabAssetDocument> CreatePrefabDocument(const Scene &scene, ECS::Entity root_entity);
        // Updates an existing Prefab document while preserving stable prefab-local entity GUIDs.
        static bool UpdatePrefabDocument(const Scene &scene, ECS::Entity root_entity, PrefabAssetDocument &prefab);
        static PrefabInstantiateResult Instantiate(Scene &scene, const PrefabAssetDocument &prefab);
        static bool RecordPropertyOverride(Scene &scene, ECS::Entity entity, StringView component, StringView property);
        static bool RevertProperty(Scene &scene, ECS::Entity entity, const PrefabAssetDocument &prefab,
                                   StringView component, StringView property);
        static bool RevertAll(Scene &scene, ECS::Entity root_entity, const PrefabAssetDocument &prefab);
        // Locates the Prefab resource and root entity from Scene authoring metadata before refreshing the instance.
        static bool Refresh(Scene &scene, ECS::Entity entity);
        static bool Refresh(Scene &scene, ECS::Entity root_entity, const PrefabAssetDocument &prefab);
        static bool RefreshProperties(Scene &scene, ECS::Entity root_entity, const PrefabAssetDocument &prefab);
        // Converts an instantiated Prefab subtree into ordinary Scene entities without changing its current content.
        static bool Unpack(Scene &scene, ECS::Entity root_entity);
    };
}

#endif
