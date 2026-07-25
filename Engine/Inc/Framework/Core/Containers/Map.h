#pragma once
#include <map>
#include <unordered_map>

namespace Ailu
{
    template<typename key, typename value>
    using Map = std::map<key, value>;

    template<typename key, typename value, typename hasher = std::hash<key>,
             typename key_equal = std::equal_to<key>,
             typename allocator = std::allocator<std::pair<const key, value>>>
    using HashMap = std::unordered_map<key, value, hasher, key_equal, allocator>;
}