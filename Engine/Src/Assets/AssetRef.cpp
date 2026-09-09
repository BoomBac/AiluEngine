#include "Assets/AssetRef.h"

namespace Ailu
{
    namespace
    {
        std::mutex s_handle_resolver_mutex;
        AssetReferenceRuntime::HandleResolver s_handle_resolver;
    }

    void AssetReferenceRuntime::SetHandleResolver(HandleResolver resolver)
    {
        std::lock_guard<std::mutex> lock(s_handle_resolver_mutex);
        s_handle_resolver = std::move(resolver);
    }

    AssetHandleBase AssetReferenceRuntime::ResolveHandle(const Guid &guid)
    {
        if (guid.IsEmpty())
            return {};

        HandleResolver resolver;
        {
            std::lock_guard<std::mutex> lock(s_handle_resolver_mutex);
            resolver = s_handle_resolver;
        }
        return resolver ? resolver(guid) : AssetHandleBase{};
    }
}
