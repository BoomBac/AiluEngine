#pragma once
#ifndef __ENTITY_SERIALIZER_H__
#define __ENTITY_SERIALIZER_H__

#include "Scene/Entity.h"

namespace Ailu
{
    class PrefabAssetDocument;
}

namespace Ailu::SceneManagement
{
    class Scene;

    class AILU_API EntitySerializer
    {
    public:
        // Clone an entity as a regular scene entity. Persistent identity and hierarchy links are always regenerated.
        static ECS::Entity CloneEntity(Scene &scene, ECS::Entity source);
        // Clone a complete hierarchy while preserving local transforms and parent-child relationships.
        static ECS::Entity CloneSubtree(Scene &scene, ECS::Entity source_root);
        // Serializes a Scene subtree into a Prefab document using Prefab-local entity GUIDs.
        static bool SerializeSubtree(const Scene &scene, ECS::Entity source_root, PrefabAssetDocument &prefab);

    private:
        static void CopyComponents(Scene &scene, ECS::Entity source, ECS::Entity target);
        static void CollectSubtree(const Scene &scene, ECS::Entity entity, Vector<ECS::Entity> &entities);
        static String AcquireDuplicateName(const Scene &scene, StringView source_name);
    };
}

#endif
