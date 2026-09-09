#pragma once

#include "Assets/AssetArtifact.h"
#include "Assets/AssetCommon.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/Guid.h"

#include <limits>
#include <functional>
#include <mutex>

namespace Ailu
{
    class Object;
    class AssetRegistry;

    inline constexpr u32 kInvalidAssetSlotIndex = std::numeric_limits<u32>::max();

    enum class EAssetLoadState : u8
    {
        kUnloaded,
        kLoading,
        kReady,
        kUpdating,
        kFailed,
    };

    enum class EAssetUpdateReason : u8
    {
        kInitialLoad,
        kSourceChanged,
        kImportSettingChanged,
        kDependencyChanged,
        kManualReimport,
        kArtifactChanged,
    };

    struct AILU_API AssetHandleBase
    {
        [[nodiscard]] bool IsValid() const;

        u32 _index = kInvalidAssetSlotIndex;
        u32 _slot_generation = 0u;

    protected:
        const AssetRegistry *_registry = nullptr;

        friend class AssetRegistry;
        template<typename T>
        friend class AssetHandle;
    };

    template<typename T>
    class AssetHandle final : public AssetHandleBase
    {
    public:
        AssetHandle() = default;
        explicit AssetHandle(const AssetHandleBase &handle)
        {
            _index = handle._index;
            _slot_generation = handle._slot_generation;
            _registry = handle._registry;
        }

        [[nodiscard]] Ref<const T> Resolve() const;
        [[nodiscard]] u64 GetRevision() const;
    };

    template<typename T>
    struct SoftAssetRef
    {
        Guid _guid = Guid::EmptyGuid();

        [[nodiscard]] bool IsAssigned() const { return !_guid.IsEmpty(); }
    };

    struct AILU_API AssetSlot
    {
        Guid _guid = Guid::EmptyGuid();
        Ref<const Object> _current;
        AssetArtifactKey _artifact_key;
        u64 _revision = 0u;
        u64 _requested_revision = 0u;
        u32 _slot_generation = 1u;
        EAssetLoadState _state = EAssetLoadState::kUnloaded;
        bool _last_update_failed = false;
        String _last_error;
        Vector<AssetDependency> _dependencies;
        Vector<AssetDependency> _dependents;
        mutable std::mutex _mutex;
    };

    struct AILU_API AssetPublishedEvent
    {
        AssetHandleBase _asset;
        u64 _old_revision = 0u;
        u64 _new_revision = 0u;
        EAssetUpdateReason _reason = EAssetUpdateReason::kInitialLoad;
    };

    class AILU_API AssetRegistry final
    {
    public:
        using PublishedListener = std::function<void(const AssetPublishedEvent &)>;

        template<typename T>
        AssetHandle<T> GetOrCreateHandle(const Guid &guid)
        {
            return MakeHandle<T>(GetOrCreateHandleBase(guid));
        }

        [[nodiscard]] bool IsValid(const AssetHandleBase &handle) const;
        [[nodiscard]] Ref<const Object> Resolve(const AssetHandleBase &handle) const;
        [[nodiscard]] u64 GetRevision(const AssetHandleBase &handle) const;
        [[nodiscard]] AssetArtifactKey GetArtifactKey(const AssetHandleBase &handle) const;
        [[nodiscard]] EAssetLoadState GetState(const AssetHandleBase &handle) const;
        [[nodiscard]] u64 MarkUpdating(const AssetHandleBase &handle);
        void MarkUpdateFailed(const AssetHandleBase &handle, String error, u64 requested_revision = 0u);
        void SetDependencies(const AssetHandleBase &handle, const Vector<AssetDependency> &dependencies);
        [[nodiscard]] Vector<AssetDependency> GetDependencies(const AssetHandleBase &handle) const;
        [[nodiscard]] Vector<AssetDependency> GetDependents(const AssetHandleBase &handle) const;
        bool Publish(const AssetHandleBase &handle, Ref<const Object> candidate, const AssetArtifactKey &artifact_key,
                     EAssetUpdateReason reason = EAssetUpdateReason::kInitialLoad, u64 requested_revision = 0u);
        u64 AddPublishedListener(PublishedListener listener);
        void RemovePublishedListener(u64 listener_id);
        void Unregister(const Guid &guid);

    private:
        AssetHandleBase GetOrCreateHandleBase(const Guid &guid);
        u32 GetOrCreateSlotIndexLocked(const Guid &guid);

        template<typename T>
        AssetHandle<T> MakeHandle(const AssetHandleBase &handle) const
        {
            AssetHandle<T> typed_handle;
            typed_handle._index = handle._index;
            typed_handle._slot_generation = handle._slot_generation;
            typed_handle._registry = handle._registry;
            return typed_handle;
        }

        AssetSlot *TryGetSlot(const AssetHandleBase &handle);
        const AssetSlot *TryGetSlot(const AssetHandleBase &handle) const;

        mutable std::mutex _mutex;
        mutable std::mutex _listener_mutex;
        Vector<Scope<AssetSlot>> _slots;
        Vector<u32> _free_slots;
        HashMap<Guid, u32, GuidHasher> _guid_to_slot;
        Map<u64, PublishedListener> _published_listeners;
        u64 _next_listener_id = 0u;
    };

    template<typename T>
    Ref<const T> AssetHandle<T>::Resolve() const
    {
        if (_registry == nullptr)
            return nullptr;
        return std::dynamic_pointer_cast<const T>(_registry->Resolve(*this));
    }

    template<typename T>
    u64 AssetHandle<T>::GetRevision() const
    {
        return _registry != nullptr ? _registry->GetRevision(*this) : 0u;
    }
}
