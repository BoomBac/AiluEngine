#ifndef AILU_SERIALIZE_SPECIALIZATIONS_H
#define AILU_SERIALIZE_SPECIALIZATIONS_H

#include "GlobalMarco.h"

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
            {
                sar->BeginObject(*name);
                FStructedArchive::EStructedDataType type;
                arr_size = sar->BeginArray(type);
                data_size = FStructedArchive::GetStructedTypeSize(type);
            }
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
            {
                sar->EndObject();
                sar->EndArray();
            }
        }
    };
}
#endif