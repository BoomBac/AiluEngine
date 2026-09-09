#pragma once

#include "Assets/AssetRegistry.h"

#include <functional>
#include <mutex>

namespace Ailu
{
    class AILU_API AssetReferenceRuntime final
    {
    public:
        using HandleResolver = std::function<AssetHandleBase(const Guid &)>;

        static void SetHandleResolver(HandleResolver resolver);
        [[nodiscard]] static AssetHandleBase ResolveHandle(const Guid &guid);
    };

    // Persistent references keep only Guid. The runtime handle resolves the current immutable
    // snapshot and therefore cannot pin an old asset version alive across a hot reload.
    template<typename T>
    class AssetRef
    {
    public:
        [[nodiscard]] const Guid &GetGuid() const noexcept { return _guid; }
        [[nodiscard]] Ref<T> Get() const
        {
            if (Ref<const T> snapshot = _handle.Resolve(); snapshot != nullptr)
                return std::const_pointer_cast<T>(snapshot);
            return _unregistered_snapshot.lock();
        }

        [[nodiscard]] bool IsAssigned() const noexcept { return !_guid.IsEmpty(); }
        [[nodiscard]] bool IsResolved() const { return _handle.Resolve() != nullptr || !_unregistered_snapshot.expired(); }
        [[nodiscard]] bool IsMissing() const { return IsAssigned() && _handle.Resolve() == nullptr; }

        void SetGuid(const Guid &guid)
        {
            _guid = guid;
            _handle = guid.IsEmpty() ? AssetHandle<T>{} : AssetHandle<T>(AssetReferenceRuntime::ResolveHandle(guid));
            _unregistered_snapshot.reset();
        }

        void Set(const Guid &guid, Ref<T> asset)
        {
            SetGuid(guid);
            _unregistered_snapshot = std::move(asset);
        }

        void Clear()
        {
            _guid = Guid::EmptyGuid();
            _handle = {};
            _unregistered_snapshot.reset();
        }

        // Compatibility helpers keep existing runtime call sites concise while the
        // reference's Guid remains the authoritative persistence value.
        AssetRef &operator=(const Ref<T> &asset)
        {
            Clear();
            _unregistered_snapshot = asset;
            return *this;
        }

        AssetRef &operator=(std::nullptr_t)
        {
            Clear();
            return *this;
        }

        [[nodiscard]] AssetHandle<T> Handle() const noexcept { return _handle; }
        [[nodiscard]] T *get() const { return Get().get(); }
        [[nodiscard]] T *operator->() const { return get(); }
        [[nodiscard]] operator Ref<T>() const { return Get(); }
        [[nodiscard]] explicit operator bool() const { return Get() != nullptr; }
        [[nodiscard]] bool operator==(std::nullptr_t) const { return Get() == nullptr; }
        [[nodiscard]] bool operator!=(std::nullptr_t) const { return Get() != nullptr; }

    private:
        Guid _guid = Guid::EmptyGuid();
        AssetHandle<T> _handle;
        Weak<T> _unregistered_snapshot;
    };
}
