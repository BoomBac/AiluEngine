#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Types.h"
#include "Framework/Math/MathHash.hpp"
#include <bitset>
#include <functional>
#include <stdexcept>

namespace Ailu::Math::ALHash
{
    template<u8 Size>
    class AILU_API Hash
    {
    public:
        Hash()
        {
            _hash.reset();
        }

        Hash(u64 hash_value)
        {
            _hash = hash_value;
        }

        void Set(u8 pos, u8 size, u64 value)
        {
            if (pos + size > Size)
            {
                throw std::out_of_range("Invalid position and size for Set operation");
            }
            while (size--)
            {
                _hash.set(pos++, value & 1);
                value >>= 1;
            }
        }

        u64 Get(u8 pos, u8 size) const
        {
            if (pos + size > Size)
            {
                throw std::out_of_range("Invalid position and size for Get operation");
            }
            std::bitset<64> result;
            for (u8 i = 0; i < size; ++i)
            {
                result.set(i, _hash.test(pos + i));
            }
            return static_cast<u64>(result.to_ullong());
        }

        bool operator==(const Hash<Size> &other) const
        {
            return _hash == other._hash;
        }

        bool operator<(const Hash<Size> &other) const
        {
            for (int i = Size - 1; i >= 0; --i)
            {
                if (_hash[i] != other._hash[i])
                    return _hash[i] < other._hash[i];
            }
            return false;
        }

        String ToString() const
        {
            return _hash.to_string();
        }

        struct HashFunc
        {
            size_t operator()(const Hash<Size> &obj) const
            {
                return std::hash<std::bitset<Size>>{}(obj._hash);
            }
        };

    private:
        std::bitset<Size> _hash;
    };

    template<typename T>
    static u32 HashFunc(const T &obj)
    {
        throw std::runtime_error("Unhandled type whth hasher");
        return 0;
    }

    template<typename T>
    struct Hasher
    {
        u64 operator()(const T &obj) const
        {
            throw std::runtime_error("Unhandled type whth hasher");
            return 0;
        }
    };

    template<typename T>
    static u32 CommonRuntimeHasher(const T &obj)
    {
        static Vector<T> hashes;
        for (u32 i = 0; i < hashes.size(); i++)
        {
            if (hashes[i] == obj)
                return i;
        }
        hashes.emplace_back(obj);
        return static_cast<u32>(hashes.size() - 1);
    }

}
