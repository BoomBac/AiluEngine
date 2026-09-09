#include "Assets/AssetRegistry.h"

#include "Objects/Object.h"

#include <algorithm>

namespace Ailu
{
    bool AssetHandleBase::IsValid() const
    {
        return _registry != nullptr && _registry->IsValid(*this);
    }

    AssetHandleBase AssetRegistry::GetOrCreateHandleBase(const Guid &guid)
    {
        if (guid.IsEmpty())
            return {};

        std::lock_guard<std::mutex> lock(_mutex);
        const u32 index = GetOrCreateSlotIndexLocked(guid);
        const AssetSlot &slot = *_slots[index];
        AssetHandleBase handle;
        handle._index = index;
        handle._slot_generation = slot._slot_generation;
        handle._registry = this;
        return handle;
    }

    u32 AssetRegistry::GetOrCreateSlotIndexLocked(const Guid &guid)
    {
        auto iter = _guid_to_slot.find(guid);
        if (iter != _guid_to_slot.end())
            return iter->second;

        u32 index = kInvalidAssetSlotIndex;
        if (!_free_slots.empty())
        {
            index = _free_slots.back();
            _free_slots.pop_back();
            AssetSlot &slot = *_slots[index];
            std::lock_guard<std::mutex> slot_lock(slot._mutex);
            ++slot._slot_generation;
            slot._guid = guid;
            slot._current.reset();
            slot._artifact_key = {};
            slot._revision = 0u;
            slot._requested_revision = 0u;
            slot._state = EAssetLoadState::kUnloaded;
            slot._last_update_failed = false;
            slot._last_error.clear();
            slot._dependencies.clear();
            slot._dependents.clear();
        }
        else
        {
            index = static_cast<u32>(_slots.size());
            _slots.emplace_back(MakeScope<AssetSlot>());
            _slots.back()->_guid = guid;
        }
        _guid_to_slot.emplace(guid, index);
        return index;
    }

    bool AssetRegistry::IsValid(const AssetHandleBase &handle) const
    {
        return TryGetSlot(handle) != nullptr;
    }

    Ref<const Object> AssetRegistry::Resolve(const AssetHandleBase &handle) const
    {
        const AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return nullptr;
        std::lock_guard<std::mutex> lock(slot->_mutex);
        if (slot->_slot_generation != handle._slot_generation)
            return nullptr;
        return slot->_current;
    }

    u64 AssetRegistry::GetRevision(const AssetHandleBase &handle) const
    {
        const AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return 0u;
        std::lock_guard<std::mutex> lock(slot->_mutex);
        if (slot->_slot_generation != handle._slot_generation)
            return 0u;
        return slot->_revision;
    }

    AssetArtifactKey AssetRegistry::GetArtifactKey(const AssetHandleBase &handle) const
    {
        const AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return {};
        std::lock_guard<std::mutex> lock(slot->_mutex);
        return slot->_slot_generation == handle._slot_generation ? slot->_artifact_key : AssetArtifactKey{};
    }

    EAssetLoadState AssetRegistry::GetState(const AssetHandleBase &handle) const
    {
        const AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return EAssetLoadState::kFailed;
        std::lock_guard<std::mutex> lock(slot->_mutex);
        if (slot->_slot_generation != handle._slot_generation)
            return EAssetLoadState::kFailed;
        return slot->_state;
    }

    u64 AssetRegistry::MarkUpdating(const AssetHandleBase &handle)
    {
        AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return 0u;
        std::lock_guard<std::mutex> lock(slot->_mutex);
        if (slot->_slot_generation != handle._slot_generation)
            return 0u;
        slot->_state = slot->_current != nullptr ? EAssetLoadState::kUpdating : EAssetLoadState::kLoading;
        return ++slot->_requested_revision;
    }

    void AssetRegistry::MarkUpdateFailed(const AssetHandleBase &handle, String error, u64 requested_revision)
    {
        AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return;
        std::lock_guard<std::mutex> lock(slot->_mutex);
        if (slot->_slot_generation != handle._slot_generation)
            return;
        if (requested_revision != 0u && requested_revision != slot->_requested_revision)
            return;
        slot->_last_update_failed = true;
        slot->_last_error = std::move(error);
        slot->_state = slot->_current != nullptr ? EAssetLoadState::kReady : EAssetLoadState::kFailed;
    }

    void AssetRegistry::SetDependencies(const AssetHandleBase &handle, const Vector<AssetDependency> &dependencies)
    {
        if (handle._registry != this)
            return;
        std::lock_guard<std::mutex> registry_lock(_mutex);
        if (handle._index >= _slots.size())
            return;
        AssetSlot &slot = *_slots[handle._index];
        std::lock_guard<std::mutex> slot_lock(slot._mutex);
        if (slot._slot_generation != handle._slot_generation)
            return;

        for (const AssetDependency &dependency: slot._dependencies)
        {
            auto dependency_iter = _guid_to_slot.find(dependency._guid);
            if (dependency_iter == _guid_to_slot.end())
                continue;
            AssetSlot &dependency_slot = *_slots[dependency_iter->second];
            std::lock_guard<std::mutex> dependency_lock(dependency_slot._mutex);
            std::erase_if(dependency_slot._dependents, [&slot](const AssetDependency &dependent)
            {
                return dependent._guid == slot._guid;
            });
        }

        slot._dependencies.clear();
        for (const AssetDependency &dependency: dependencies)
        {
            if (dependency._guid.IsEmpty() || dependency._guid == slot._guid)
                continue;
            const bool duplicate = std::any_of(slot._dependencies.begin(), slot._dependencies.end(),
                                               [&dependency](const AssetDependency &existing)
                                               { return existing._guid == dependency._guid; });
            if (duplicate)
                continue;

            slot._dependencies.emplace_back(dependency);
            AssetSlot &dependency_slot = *_slots[GetOrCreateSlotIndexLocked(dependency._guid)];
            std::lock_guard<std::mutex> dependency_lock(dependency_slot._mutex);
            dependency_slot._dependents.emplace_back(AssetDependency{slot._guid, dependency._type});
        }
    }

    Vector<AssetDependency> AssetRegistry::GetDependencies(const AssetHandleBase &handle) const
    {
        const AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return {};
        std::lock_guard<std::mutex> lock(slot->_mutex);
        return slot->_slot_generation == handle._slot_generation ? slot->_dependencies : Vector<AssetDependency>{};
    }

    Vector<AssetDependency> AssetRegistry::GetDependents(const AssetHandleBase &handle) const
    {
        const AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr)
            return {};
        std::lock_guard<std::mutex> lock(slot->_mutex);
        return slot->_slot_generation == handle._slot_generation ? slot->_dependents : Vector<AssetDependency>{};
    }

    bool AssetRegistry::Publish(const AssetHandleBase &handle, Ref<const Object> candidate,
                                const AssetArtifactKey &artifact_key, EAssetUpdateReason reason, u64 requested_revision)
    {
        AssetSlot *slot = TryGetSlot(handle);
        if (slot == nullptr || candidate == nullptr)
            return false;

        AssetPublishedEvent event;
        {
            std::lock_guard<std::mutex> lock(slot->_mutex);
            if (slot->_slot_generation != handle._slot_generation)
                return false;
            if (requested_revision != 0u && requested_revision != slot->_requested_revision)
                return false;
            event._asset = handle;
            event._old_revision = slot->_revision;
            event._new_revision = ++slot->_revision;
            event._reason = reason;
            slot->_current = std::move(candidate);
            slot->_artifact_key = artifact_key;
            slot->_state = EAssetLoadState::kReady;
            slot->_last_update_failed = false;
            slot->_last_error.clear();
        }

        Vector<PublishedListener> listeners;
        {
            std::lock_guard<std::mutex> lock(_listener_mutex);
            listeners.reserve(_published_listeners.size());
            for (const auto &[listener_id, listener]: _published_listeners)
                listeners.emplace_back(listener);
        }
        for (const PublishedListener &listener: listeners)
            listener(event);
        return true;
    }

    u64 AssetRegistry::AddPublishedListener(PublishedListener listener)
    {
        if (!listener)
            return 0u;
        std::lock_guard<std::mutex> lock(_listener_mutex);
        const u64 listener_id = ++_next_listener_id;
        _published_listeners.emplace(listener_id, std::move(listener));
        return listener_id;
    }

    void AssetRegistry::RemovePublishedListener(u64 listener_id)
    {
        if (listener_id == 0u)
            return;
        std::lock_guard<std::mutex> lock(_listener_mutex);
        _published_listeners.erase(listener_id);
    }

    void AssetRegistry::Unregister(const Guid &guid)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto iter = _guid_to_slot.find(guid);
        if (iter == _guid_to_slot.end())
            return;
        AssetSlot &slot = *_slots[iter->second];
        {
            std::lock_guard<std::mutex> slot_lock(slot._mutex);
            slot._guid = Guid::EmptyGuid();
            slot._current.reset();
            slot._state = EAssetLoadState::kUnloaded;
        }
        _free_slots.emplace_back(iter->second);
        _guid_to_slot.erase(iter);
    }

    AssetSlot *AssetRegistry::TryGetSlot(const AssetHandleBase &handle)
    {
        return const_cast<AssetSlot *>(std::as_const(*this).TryGetSlot(handle));
    }

    const AssetSlot *AssetRegistry::TryGetSlot(const AssetHandleBase &handle) const
    {
        if (handle._registry != this)
            return nullptr;
        std::lock_guard<std::mutex> lock(_mutex);
        if (handle._index >= _slots.size())
            return nullptr;
        const AssetSlot *slot = _slots[handle._index].get();
        std::lock_guard<std::mutex> slot_lock(slot->_mutex);
        return slot->_slot_generation == handle._slot_generation ? slot : nullptr;
    }
}
