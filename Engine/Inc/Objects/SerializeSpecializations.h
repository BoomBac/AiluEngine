#ifndef AILU_SERIALIZE_SPECIALIZATIONS_H
#define AILU_SERIALIZE_SPECIALIZATIONS_H

#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"

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

    template<>
    struct AILU_API SerializerWrapper<String>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr);
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr);
    };

    template<typename T>
    struct AILU_API SerializerWrapper<Vector<T>>
    {
        static void Serialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_S(Vector<T>)
            FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
            Vector<T> *vec = static_cast<Vector<T> *>(data);
            if (name)
                sar->BeginObject(*name);
            if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, String>)
            {
                // 基础类型数组批量处理
                sar->BeginArray(vec->size(), FStructedArchive::GetStructedType<T>());
                String item_name{};
                u32 count = 0u;
                for (auto &item: *vec)
                {
                    item_name = std::format("arr_item_{}", count++);
                    SerializerWrapper<T>::Serialize(&item, ar, &item_name);
                }
                sar->EndArray();
            }
            else
            {
                // 结构体数组逐元素处理
                sar->BeginArray(vec->size(), FStructedArchive::EStructedDataType::kStruct);
                String item_name{};
                u32 count = 0u;
                for (auto &item: *vec)
                {
                    item_name = std::format("arr_item_{}", count++);
                    SerializerWrapper<T>::Serialize(&item, ar, &item_name);
                }
                sar->EndArray();
            }
            if (name)
                sar->EndObject();
        }
        static void Deserialize(void *data, FArchive &ar, const String *name = nullptr)
        {
            DATA_CHECK_DS(Vector<T>)
            FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
            Vector<T> *out_vec = static_cast<Vector<T> *>(data);
            u64 arr_size = 0u,data_size = 0u;
            if (sar && name)
                sar->BeginObject(*name);
            FStructedArchive::EStructedDataType type;
            arr_size = sar->BeginArray(type);
            data_size = FStructedArchive::GetStructedTypeSize(type);
            if (arr_size != 0u && data_size != 0u)
            {
                out_vec->resize(arr_size);
                String item_name{};
                for (u64 i = 0; i < arr_size; i++)
                {
                    item_name = std::to_string(i);
                    SerializerWrapper<T>::Deserialize(&(*out_vec)[i], ar, &item_name);
                }
            }
            if (sar && name)
                sar->EndObject();
            sar->EndArray();
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