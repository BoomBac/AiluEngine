#pragma once
#ifndef __ENTITY_REFERENCE_H__
#define __ENTITY_REFERENCE_H__
#include "Framework/Core/String.h"
#include "Framework/Math/Guid.h"
#include "Scene/Entity.h"
#include "Objects/Serialize.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    // Persistent entity reference: stable identity that survives scene save/load and rebuild.
    // Hot paths (render, system iteration) keep using ECS::Entity; this type is only for
    // persistence boundaries, cross-lifecycle references and editor diagnostics.
    struct AILU_API EntityReference
    {
        inline static const String kSceneGuid = "_scene_guid";
        inline static const String kEntityGuid = "_entity_guid";

        Guid _scene_guid;   // Scene asset GUID; may be empty for same-scene references.
        Guid _entity_guid;  // Persistent entity GUID; empty means not referencing anything.

        // Resolve to the runtime handle in the given scene, or ECS::kInvalidEntity if unresolvable.
        ECS::Entity Resolve(const SceneManagement::Scene &scene) const;
        bool IsValid() const;
    };

    template<>
    struct AILU_API SerializerWrapper<EntityReference>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr);
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr);
    };
}

#endif// !__ENTITY_REFERENCE_H__
