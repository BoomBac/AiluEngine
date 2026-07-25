#pragma once

#include <memory>
#include <utility>

namespace Ailu
{
    template<typename T>
    using Scope = std::unique_ptr<T>;

    template<typename T>
    using Ref = std::shared_ptr<T>;

    template<typename T>
    using Weak = std::weak_ptr<T>;

    template<typename T, typename... args>
    Scope<T> MakeScope(args &&...values)
    {
        return std::make_unique<T>(std::forward<args>(values)...);
    }

    template<typename T, typename... args>
    Ref<T> MakeRef(args &&...values)
    {
        return std::make_shared<T>(std::forward<args>(values)...);
    }
}