#ifndef AILU_SERIALIZE_SPECIALIZATIONS_H
#define AILU_SERIALIZE_SPECIALIZATIONS_H

#include "Objects/Serialize.h"
#include "Objects/JsonArchive.h"
#include "Framework/Math/ALMath.hpp"
#include "Framework/Math/Guid.h"
#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Ailu
{

#define DATA_CHECK_S(msg)                                                 \
    if (data == nullptr)                                                  \
    {                                                                     \
        LOG_ERROR("SerializerWrapper<{}>::Serialize: data is null", #msg) \
        return;                                                           \
    }

#define DATA_CHECK_DS(msg)                                                  \
    if (data == nullptr)                                                    \
    {                                                                       \
        LOG_ERROR("SerializerWrapper<{}>::Deserialize: data is null", #msg) \
        return;                                                             \
    }

    template<typename T>
    struct SerializerWrapper;

    namespace Detail
    {
        template<typename T>
        inline constexpr bool kAlwaysFalse = false;

        inline FStructedArchive *RequireStructedArchive(FArchive &ar, const char *action)
        {
            auto *sar = dynamic_cast<FStructedArchive *>(&ar);
            if (sar == nullptr)
                LOG_ERROR("SerializerWrapper container {} requires FStructedArchive", action)
            return sar;
        }

        inline String MakeArrayWriteItemName(u64 index)
        {
            return std::format("arr_item_{}", index);
        }

        inline String MakeArrayReadItemName(u64 index)
        {
            return std::to_string(index);
        }

        template<typename Container>
        void ReserveIfSupported(Container &container, size_t size)
        {
            if constexpr (requires { container.reserve(size); })
                container.reserve(size);
        }

        template<typename Value>
        void SerializeValue(const Value &value, FArchive &ar, const String &name)
        {
            using MutableValue = std::remove_const_t<Value>;
            SerializerWrapper<MutableValue>::Serialize(const_cast<MutableValue *>(std::addressof(value)), ar, &name);
        }

        template<typename Value>
        void DeserializeValue(Value &value, FArchive &ar, const String &name)
        {
            SerializerWrapper<Value>::Deserialize(std::addressof(value), ar, &name);
        }

        template<typename Container, typename Value>
        void AppendValue(Container &container, Value &&value)
        {
            if constexpr (requires { container.push_back(std::forward<Value>(value)); })
            {
                container.push_back(std::forward<Value>(value));
            }
            else if constexpr (requires { container.insert(std::forward<Value>(value)); })
            {
                container.insert(std::forward<Value>(value));
            }
            else
            {
                static_assert(kAlwaysFalse<Container>, "Unsupported container insertion pattern");
            }
        }

        template<typename Element>
        constexpr FStructedArchive::EStructedDataType GetArrayElementType()
        {
            if constexpr (std::is_arithmetic_v<Element> || std::is_same_v<Element, String>)
                return FStructedArchive::GetStructedType<Element>();
            else
                return FStructedArchive::EStructedDataType::kStruct;
        }

        template<typename Container, typename Element>
        void SerializeArrayLikeContainer(void *data, FArchive &ar, const String *name)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper container Serialize: data is null");
                return;
            }

            auto *sar = RequireStructedArchive(ar, "Serialize");
            if (sar == nullptr)
                return;

            auto *container = static_cast<Container *>(data);
            if (name)
                sar->BeginObject(*name);

            sar->BeginArray(container->size(), GetArrayElementType<Element>());
            u64 count = 0u;
            for (const auto &item: *container)
            {
                const String item_name = MakeArrayWriteItemName(count++);
                SerializeValue(item, ar, item_name);
            }
            sar->EndArray();

            if (name)
                sar->EndObject();
        }

        template<typename Container, typename Element>
        void DeserializeArrayLikeContainer(void *data, FArchive &ar, const String *name)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper container Deserialize: data is null");
                return;
            }

            auto *sar = RequireStructedArchive(ar, "Deserialize");
            if (sar == nullptr)
                return;

            auto *container = static_cast<Container *>(data);
            if (name)
                sar->BeginObject(*name);

            FStructedArchive::EStructedDataType type{};
            const u64 arr_size = sar->BeginArray(type);
            container->clear();
            ReserveIfSupported(*container, static_cast<size_t>(arr_size));
            for (u64 i = 0; i < arr_size; ++i)
            {
                Element item{};
                const String item_name = MakeArrayReadItemName(i);
                DeserializeValue(item, ar, item_name);
                AppendValue(*container, std::move(item));
            }

            sar->EndArray();
            if (name)
                sar->EndObject();
        }

        template<typename Key, typename Value>
        void SerializeMapEntry(const Key &key, const Value &value, FArchive &ar, const String &name)
        {
            auto *sar = static_cast<FStructedArchive *>(&ar);
            sar->BeginObject(name);
            static const String key_name = "key";
            static const String value_name = "value";
            SerializeValue(key, ar, key_name);
            SerializeValue(value, ar, value_name);
            sar->EndObject();
        }

        template<typename Key, typename Value>
        void DeserializeMapEntry(Key &key, Value &value, FArchive &ar, const String &name)
        {
            auto *sar = static_cast<FStructedArchive *>(&ar);
            sar->BeginObject(name);
            static const String key_name = "key";
            static const String value_name = "value";
            DeserializeValue(key, ar, key_name);
            DeserializeValue(value, ar, value_name);
            sar->EndObject();
        }

        template<typename Container, typename Key, typename Value>
        void SerializeMapLikeContainer(void *data, FArchive &ar, const String *name)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper map Serialize: data is null");
                return;
            }

            auto *sar = RequireStructedArchive(ar, "Serialize");
            if (sar == nullptr)
                return;

            auto *container = static_cast<Container *>(data);
            if (name)
                sar->BeginObject(*name);

            sar->BeginArray(container->size(), FStructedArchive::EStructedDataType::kStruct);
            u64 count = 0u;
            for (const auto &entry: *container)
            {
                const String item_name = MakeArrayWriteItemName(count++);
                SerializeMapEntry(entry.first, entry.second, ar, item_name);
            }
            sar->EndArray();

            if (name)
                sar->EndObject();
        }

        template<typename Container, typename Key, typename Value>
        void DeserializeMapLikeContainer(void *data, FArchive &ar, const String *name)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper map Deserialize: data is null");
                return;
            }

            auto *sar = RequireStructedArchive(ar, "Deserialize");
            if (sar == nullptr)
                return;

            auto *container = static_cast<Container *>(data);
            if (name)
                sar->BeginObject(*name);

            if constexpr (std::is_same_v<std::remove_cv_t<Key>, String>)
            {
                if (auto *json_ar = dynamic_cast<JsonArchive *>(&ar); json_ar != nullptr && json_ar->IsCurrentNodeObject())
                {
                    Vector<String> keys = json_ar->GetCurrentObjectKeys();
                    container->clear();
                    ReserveIfSupported(*container, keys.size());
                    for (const String &key: keys)
                    {
                        Value value{};
                        DeserializeValue(value, ar, key);
                        container->emplace(key, std::move(value));
                    }

                    if (name)
                        sar->EndObject();
                    return;
                }
            }

            FStructedArchive::EStructedDataType type{};
            const u64 arr_size = sar->BeginArray(type);
            container->clear();
            ReserveIfSupported(*container, static_cast<size_t>(arr_size));
            for (u64 i = 0; i < arr_size; ++i)
            {
                Key key{};
                Value value{};
                const String item_name = MakeArrayReadItemName(i);
                DeserializeMapEntry(key, value, ar, item_name);
                container->emplace(std::move(key), std::move(value));
            }

            sar->EndArray();
            if (name)
                sar->EndObject();
        }
    }// namespace Detail

    template<>
    struct AILU_API SerializerWrapper<String>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr);
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr);
    };

    template<>
    struct AILU_API SerializerWrapper<WString>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr);
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr);
    };

    template<>
    struct AILU_API SerializerWrapper<Guid>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr);
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr);
    };

    template<typename T, typename Allocator>
    struct AILU_API SerializerWrapper<std::vector<T, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeArrayLikeContainer<std::vector<T, Allocator>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeArrayLikeContainer<std::vector<T, Allocator>, T>(data, ar, name);
        }
    };

    template<typename T, typename Allocator>
    struct AILU_API SerializerWrapper<std::list<T, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeArrayLikeContainer<std::list<T, Allocator>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeArrayLikeContainer<std::list<T, Allocator>, T>(data, ar, name);
        }
    };

    template<typename T, typename Allocator>
    struct AILU_API SerializerWrapper<std::deque<T, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeArrayLikeContainer<std::deque<T, Allocator>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeArrayLikeContainer<std::deque<T, Allocator>, T>(data, ar, name);
        }
    };

    template<typename Key, typename Compare, typename Allocator>
    struct AILU_API SerializerWrapper<std::set<Key, Compare, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeArrayLikeContainer<std::set<Key, Compare, Allocator>, Key>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeArrayLikeContainer<std::set<Key, Compare, Allocator>, Key>(data, ar, name);
        }
    };

    template<typename Key, typename Hash, typename KeyEqual, typename Allocator>
    struct AILU_API SerializerWrapper<std::unordered_set<Key, Hash, KeyEqual, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeArrayLikeContainer<std::unordered_set<Key, Hash, KeyEqual, Allocator>, Key>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeArrayLikeContainer<std::unordered_set<Key, Hash, KeyEqual, Allocator>, Key>(data, ar, name);
        }
    };

    template<typename Key, typename Value, typename Compare, typename Allocator>
    struct AILU_API SerializerWrapper<std::map<Key, Value, Compare, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeMapLikeContainer<std::map<Key, Value, Compare, Allocator>, Key, Value>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeMapLikeContainer<std::map<Key, Value, Compare, Allocator>, Key, Value>(data, ar, name);
        }
    };

    template<typename Key, typename Value, typename Hash, typename KeyEqual, typename Allocator>
    struct AILU_API SerializerWrapper<std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeMapLikeContainer<std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>, Key, Value>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::DeserializeMapLikeContainer<std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>, Key, Value>(data, ar, name);
        }
    };

    template<typename T, size_t N>
    struct AILU_API SerializerWrapper<std::array<T, N>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            Detail::SerializeArrayLikeContainer<std::array<T, N>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            if (data == nullptr)
            {
                LOG_ERROR("SerializerWrapper std::array Deserialize: data is null");
                return;
            }

            auto *sar = Detail::RequireStructedArchive(ar, "Deserialize");
            if (sar == nullptr)
                return;

            auto *array = static_cast<std::array<T, N> *>(data);
            if (name)
                sar->BeginObject(*name);

            FStructedArchive::EStructedDataType type{};
            const u64 arr_size = sar->BeginArray(type);
            const u64 read_count = std::min<u64>(arr_size, N);
            for (u64 i = 0; i < read_count; ++i)
            {
                const String item_name = Detail::MakeArrayReadItemName(i);
                Detail::DeserializeValue((*array)[i], ar, item_name);
            }
            for (u64 i = read_count; i < N; ++i)
            {
                (*array)[i] = T{};
            }
            if (arr_size != N)
            {
                LOG_WARNING("SerializerWrapper<std::array>::Deserialize size mismatch, archive: {}, target: {}", arr_size, N)
            }

            sar->EndArray();
            if (name)
                sar->EndObject();
        }
    };

    template<size_t N, typename VecType, typename T>
    static void SerializeVectorND(void *data, FArchive &ar, const String *name)
    {
        Vector<T> tmp;
        auto &value = *static_cast<VecType *>(data);
        for (size_t i = 0; i < N; ++i)
            tmp.push_back(value[(u32)i]);
        SerializerWrapper<Vector<T>>::Serialize(&tmp, ar, name);
    }

    template<size_t N, typename VecType, typename T>
    static void DeserializeVectorND(void *data, FArchive &ar, const String *name)
    {
        Vector<T> tmp;
        SerializerWrapper<Vector<T>>::Deserialize(&tmp, ar, name);
        auto &value = *static_cast<VecType *>(data);
        for (size_t i = 0; i < N; ++i)
            value[(u32)i] = tmp[i];
    }


    template<typename T>
    struct AILU_API SerializerWrapper<Vector2D<T>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_S(Vector2D)
            SerializeVectorND<2, Vector2D<T>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_DS(Vector2D)
            DeserializeVectorND<2, Vector2D<T>, T>(data, ar, name);
        }
    };

    template<typename T>
    struct AILU_API SerializerWrapper<Vector3D<T>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_S(Vector3D)
            SerializeVectorND<3, Vector3D<T>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_DS(Vector3D)
            DeserializeVectorND<3, Vector3D<T>, T>(data, ar, name);
        }
    };

    template<typename T>
    struct AILU_API SerializerWrapper<Vector4D<T>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_S(Vector4D)
            SerializeVectorND<4, Vector4D<T>, T>(data, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_DS(Vector4D)
            DeserializeVectorND<4, Vector4D<T>, T>(data, ar, name);
        }
    };

    template<>
    struct AILU_API SerializerWrapper<Quaternion>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_S(Quaternion)
            SerializerWrapper<Vector4D<f32>>::Serialize(&static_cast<Quaternion *>(data)->_quat, ar, name);
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_DS(Quaternion)
            SerializerWrapper<Vector4D<f32>>::Deserialize(&static_cast<Quaternion *>(data)->_quat, ar, name);
        }
    };

}
#endif
