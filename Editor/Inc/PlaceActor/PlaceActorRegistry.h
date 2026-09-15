#pragma once

#include "Framework/Common/NonCopyable.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Math/Vector.hpp"
#include "Scene/Entity.h"

#include <functional>

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;
    }

    namespace Editor
    {
        struct PlaceActorSpawnContext
        {
            SceneManagement::Scene *_scene = nullptr;
            Vector3f _position = Vector3f::kZero;
        };

        using PlaceActorSpawnFunc = std::function<ECS::Entity(const PlaceActorSpawnContext &)>;

        struct PlaceActorDesc
        {
            String _id;
            String _display_name;
            String _category;
            i32 _order = 0;
            PlaceActorSpawnFunc _spawn;
        };

        class PlaceActorRegistry final : public NonCopyable
        {
        public:
            static PlaceActorRegistry &Get();

            void Register(PlaceActorDesc desc);
            const PlaceActorDesc *Find(const String &id) const;
            ECS::Entity Spawn(const String &id, const PlaceActorSpawnContext &context) const;

            const Vector<PlaceActorDesc> &Entries() const { return _entries; }

        private:
            Vector<PlaceActorDesc> _entries;
            HashMap<String, u32> _entry_indices;
        };
    }// namespace Editor
}// namespace Ailu
