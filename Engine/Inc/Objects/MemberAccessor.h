#ifndef __MEMBER_ACCESS_H__
#define __MEMBER_ACCESS_H__
#pragma once
#include "GlobalMarco.h"
namespace Ailu
{
    class IMemberAccessor
    {
    public:
        virtual ~IMemberAccessor() = default;
    };

    class PropertyAccessor : public IMemberAccessor
    {
    public:
        virtual void *GetVoidPtr(void *instance) const = 0;
        virtual const void *GetVoidPtr(const void *instance) const = 0;
    };

    class FunctionInvoker : public IMemberAccessor
    {
    public:
        // 通用变参调用：传入实参数组（指针数组）和可选返回值指针
        virtual void Invoke(void *instance, void **args, void *retVal) const = 0;
    };

    class OffsetPropertyAccessor : public PropertyAccessor
    {
    public:
        explicit OffsetPropertyAccessor(u64 offset) : _offset(offset) {}

        void *GetVoidPtr(void *instance) const override
        {
            return static_cast<u8 *>(instance) + _offset;
        }
        const void *GetVoidPtr(const void *instance) const override
        {
            return static_cast<const u8 *>(instance) + _offset;
        }

    private:
        u64 _offset = 0;
    };

    template<typename T, typename ClassType>
    class TypedPropertyAccessor : public PropertyAccessor
    {
    public:
        TypedPropertyAccessor(uint64_t offset) : _offset(offset) {}

        // 读取字段
        T &Get(ClassType &instance) const
        {
            return *reinterpret_cast<T *>(reinterpret_cast<u8 *>(&instance) + _offset);
        }

        const T &Get(const ClassType &instance) const
        {
            return *reinterpret_cast<const T *>(reinterpret_cast<const u8 *>(&instance) + _offset);
        }

        // 写入字段
        void Set(ClassType &instance, const T &value) const
        {
            *reinterpret_cast<T *>(reinterpret_cast<u8 *>(&instance) + _offset) = value;
        }

    private:
        u64 _offset;
    };


    // 支持无捕获：Return(Class::*)(Args...)
    template<typename ClassType, typename ReturnType, typename... Args>
    class MemberFunctionInvoker : public FunctionInvoker
    {
    public:
        using MemFn = ReturnType (ClassType::*)(Args...);
        explicit MemberFunctionInvoker(MemFn fn) : _fn(fn) {}

        void Invoke(void *instance, void **args, void *retVal) const override
        {
            if (!_fn) throw std::runtime_error("Member function pointer is null");
            ClassType *obj = static_cast<ClassType *>(instance);
            // 展开参数
            call<0, Args...>(obj, retVal, args);
        }

    private:
        template<std::size_t I, typename A, typename... Rest>
        void call(ClassType *obj, void *retVal, void **args) const
        {
            if constexpr (sizeof...(Rest) == 0)
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    ((*obj).*_fn)(*static_cast<A *>(args[I]));
                }
                else
                {
                    *static_cast<ReturnType *>(retVal) =
                            ((*obj).*_fn)(*static_cast<A *>(args[I]));
                }
            }
            else
            {
                auto binder = [this, obj, retVal, args]<typename... Packed>(Packed &&...packed)
                {
                    if constexpr (std::is_void_v<ReturnType>)
                    {
                        ((*obj).*_fn)(std::forward<Packed>(packed)..., *static_cast<typename std::tuple_element<I, std::tuple<A, Rest...>>::type *>(args[I]));
                    }
                    else
                    {
                        *static_cast<ReturnType *>(retVal) =
                                ((*obj).*_fn)(std::forward<Packed>(packed)..., *static_cast<typename std::tuple_element<I, std::tuple<A, Rest...>>::type *>(args[I]));
                    }
                };
                // 递归展开（简单起见，使用指数展开也可；生产中可改更稳健实参打包）
                // 为避免模板体积，这里建议你按需要定制。
            }
        }

        MemFn _fn = nullptr;
    };
}
#endif//__MEMBER_ACCESS_H__