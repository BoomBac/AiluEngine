#pragma once

#include "Framework/Core/SmartPtr.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
    // Persistent asset references deliberately keep their Guid when the runtime object
    // cannot currently be resolved.  This is the distinction between an unassigned
    // reference and a missing reference.
    template<typename T>
    class AssetRef
    {
    public:
        [[nodiscard]] const Guid &GetGuid() const noexcept { return _guid; }
        [[nodiscard]] const Ref<T> &Get() const noexcept { return _asset; }

        [[nodiscard]] bool IsAssigned() const noexcept { return !_guid.IsEmpty(); }
        [[nodiscard]] bool IsResolved() const noexcept { return IsAssigned() && _asset != nullptr; }
        [[nodiscard]] bool IsMissing() const noexcept { return IsAssigned() && _asset == nullptr; }

        void SetGuid(const Guid &guid)
        {
            _guid = guid;
            _asset.reset();
        }

        void Set(const Guid &guid, Ref<T> asset)
        {
            _guid = guid;
            _asset = std::move(asset);
        }

        void Clear()
        {
            _guid = Guid::EmptyGuid();
            _asset.reset();
        }

        // Compatibility helpers keep existing runtime call sites concise while the
        // reference's Guid remains the authoritative persistence value.
        AssetRef &operator=(const Ref<T> &asset)
        {
            _guid = Guid::EmptyGuid();
            _asset = asset;
            return *this;
        }

        AssetRef &operator=(std::nullptr_t)
        {
            Clear();
            return *this;
        }

        [[nodiscard]] T *get() const noexcept { return _asset.get(); }
        [[nodiscard]] T *operator->() const noexcept { return _asset.get(); }
        [[nodiscard]] operator const Ref<T> &() const noexcept { return _asset; }
        [[nodiscard]] explicit operator bool() const noexcept { return _asset != nullptr; }
        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept { return _asset == nullptr; }
        [[nodiscard]] bool operator!=(std::nullptr_t) const noexcept { return _asset != nullptr; }

    private:
        Guid _guid = Guid::EmptyGuid();
        Ref<T> _asset;
    };
}
