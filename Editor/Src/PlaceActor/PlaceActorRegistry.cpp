#include "PlaceActor/PlaceActorRegistry.h"

#include "Framework/Common/Assert.h"
#include "Framework/Common/Log.h"

#include <utility>

namespace Ailu
{
    namespace Editor
    {
        PlaceActorRegistry &PlaceActorRegistry::Get()
        {
            static PlaceActorRegistry s_registry;
            return s_registry;
        }

        void PlaceActorRegistry::Register(PlaceActorDesc desc)
        {
            if (desc._id.empty())
            {
                LOG_ERROR("PlaceActorRegistry: cannot register an empty id.");
                AL_ASSERT(false);
                return;
            }
            if (!desc._spawn)
            {
                LOG_ERROR("PlaceActorRegistry: spawn callback is missing for '{}'.", desc._id);
                AL_ASSERT(false);
                return;
            }
            if (_entry_indices.find(desc._id) != _entry_indices.end())
            {
                LOG_ERROR("PlaceActorRegistry: duplicate registration for '{}'.", desc._id);
                AL_ASSERT(false);
                return;
            }

            const u32 entry_index = static_cast<u32>(_entries.size());
            _entry_indices.emplace(desc._id, entry_index);
            _entries.emplace_back(std::move(desc));
        }

        const PlaceActorDesc *PlaceActorRegistry::Find(const String &id) const
        {
            const auto it = _entry_indices.find(id);
            if (it == _entry_indices.end())
                return nullptr;
            return &_entries[it->second];
        }

        ECS::Entity PlaceActorRegistry::Spawn(const String &id, const PlaceActorSpawnContext &context) const
        {
            if (context._scene == nullptr)
            {
                LOG_WARNING("PlaceActorRegistry: cannot spawn '{}' without a scene.", id);
                return ECS::kInvalidEntity;
            }

            const PlaceActorDesc *desc = Find(id);
            if (desc == nullptr || !desc->_spawn)
            {
                LOG_WARNING("PlaceActorRegistry: unknown or invalid place actor '{}'.", id);
                return ECS::kInvalidEntity;
            }

            const ECS::Entity entity = desc->_spawn(context);
            if (entity == ECS::kInvalidEntity)
                LOG_WARNING("PlaceActorRegistry: spawn failed for '{}'.", id);
            return entity;
        }
    }// namespace Editor
}// namespace Ailu
