#pragma once
#if !defined(RDGPOOL_H)
#define RDGPOOL_H

#include "Framework/Common/Allocator.hpp"
#include "Framework/Common/Log.h"

//#include "Render/Texture.h"

namespace Ailu::Render
{
    template<typename T>
    class TResourcePool
    {
    public:
        TResourcePool(u64 capacity) : _capacity(capacity)
        {
            _resources.reserve(_capacity);
            _states.resize(_capacity, 1);
            for (u64 i = 0; i < _capacity; ++i)
            {
                _resources.emplace_back(AL_NEW(T));
            }
        }
        ~TResourcePool()
        {
            for (auto &res: _resources)
            {
                AL_DELETE(res);
            }
            _resources.clear();
            _states.clear();
        }

        T *Alloc()
        {
            for (u64 i = 0; i < _resources.size(); ++i)
            {
                if (_states[i] != 0)
                {
                    _states[i] = 0;
                    T::Reset(_resources[i]);
                    return _resources[i];
                }
            }
            _resources.emplace_back(AL_NEW(T));
            _states.push_back(0);
            ++_capacity;
            LOG_WARNING("TResourcePool: Resource pool is full, increasing capacity to {}", _capacity + 1);
            return _resources.back();
        }
        void Release(T *resource)
        {
            for (u64 i = 0; i < _resources.size(); ++i)
            {
                if (_resources[i] == resource)
                {
                    _states[i] = 1;
                    return;
                }
            }
            LOG_ERROR("TResourcePool: Attempted to release a resource that does not belong to this pool.");
        }

    private:
        u64 _capacity = 0u;
        Vector<T *> _resources;
        Vector<u8> _states;
    };

    namespace FrameHelper
    {
        u64 GetFrameCout();
    }

    /// <summary>
    /// none-thread-safe resource pool, based on hash key.
    /// </summary>
    /// <typeparam name="Key"></typeparam>
    /// <typeparam name="T"></typeparam>
    /// <typeparam name="Hasher">Ailu::Math::ALHash::Hasher<Key>></typeparam>
    template<typename Key, typename T, typename Hasher = Ailu::Math::ALHash::Hasher<Key>>
    class THashableResourcePool
    {
        struct ResourceHandle
        {
            Ref<T> _res;
            bool _is_available;
            u64 _last_access_frame_count;
        };
    public:
        struct PoolResourceHandle
        {
            u64 _key_hash;
            T *_res;
        };
        THashableResourcePool() = default;
        ~THashableResourcePool() 
        {
            _pool.clear();
        };
        /// <summary>
        /// 根据key注册资源到池中，转移资源所有权,并将其标记为不可用
        /// </summary>
        /// <param name="key"></param>
        /// <param name="res"></param>
        PoolResourceHandle Add(const Key &key, Ref<T> res)
        {
            const u64 hash = _static_hasher(key);
            ResourceHandle new_handle;
            new_handle._is_available = false;
            new_handle._res = res;
            new_handle._last_access_frame_count = FrameHelper::GetFrameCout();
            _pool.insert({hash, new_handle});
            return {hash, res.get()};
        }

        /// <summary>
        /// 获取一个资源，如果没有可用的资源，则返回std::nullopt，意味着需要创建一个新的资源。
        /// </summary>
        /// <param name="key"></param>
        /// <returns></returns>
        std::optional<PoolResourceHandle> Get(const Key &key)
        {
            const u64 hash = _static_hasher(key);
            auto range = _pool.equal_range(hash);
            const u64 cur_frame = FrameHelper::GetFrameCout();
            T *res = nullptr;
            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second._is_available)
                {
                    it->second._is_available = false;
                    it->second._last_access_frame_count = cur_frame;
                    res = it->second._res.get();
                    break;
                }
                else
                {
                    if (cur_frame - it->second._last_access_frame_count > 30u)
                    {
                        it->second._last_access_frame_count = cur_frame;
                        res = it->second._res.get();
                        break;
                    }
                }
            }
            if (res)
                return std::make_optional<PoolResourceHandle>(hash, res);
            return std::nullopt;
        }

        void Release(const PoolResourceHandle &handle)
        {
            auto range = _pool.equal_range(handle._key_hash);
            for (auto& it = range.first; it != range.second; ++it)
            {
                if (it->second._res.get() == handle._res)
                {
                    it->second._is_available = true;
                    return;
                }
            }
            LOG_ERROR("THashableResourcePool: Attempted to release a resource that does not belong to this pool.");
        }

        /// <summary>销毁长期未使用的资源</summary>
        void ReleaseUnused()
        {
            const u64 cur_frame = FrameHelper::GetFrameCout();
            u32 released_rt_num = 0;
            for (auto it = _pool.begin(); it != _pool.end();)
            {
                if (!it->second._is_available)
                {
                    ++it;
                    continue;
                }
                u64 inactiveFrames = cur_frame - it->second._last_access_frame_count;
                if (inactiveFrames > MaxSleepFrames)
                {
                    it = _pool.erase(it);
                    ++released_rt_num;
                }
                else
                {
                    ++it;
                }
            }
            LOG_INFO("THashableResourcePool: Released {} unused resources", released_rt_num);
        }

        auto begin() { return _pool.begin(); }
        auto end() { return _pool.end(); }
        u32 Size() const { return static_cast<u32>(_pool.size()); }
    private:
        static constexpr u64 MaxSleepFrames = 500;
    private:
        using ResPool = std::unordered_multimap<u64, ResourceHandle>;
        ResPool _pool;
        Hasher _static_hasher;
    };

}// namespace Ailu::Render::RDG
#endif