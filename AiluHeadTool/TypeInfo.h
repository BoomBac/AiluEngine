#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct FieldInfo
{
    std::string name;
    std::function<void *(void *)> getter;      // 用于获取成员变量的指针
    std::function<void(void *, void *)> setter;// 用于设置成员变量值
};

struct MethodInfo
{
    std::string name;
    std::function<void(void *)> invoker;// 用于调用成员函数
};

struct TypeInfo
{
    std::string typeName;
    std::unordered_map<std::string, FieldInfo> fields;
    std::unordered_map<std::string, MethodInfo> methods;
};
class TypeRegistry
{
public:
    static TypeRegistry &Instance()
    {
        static TypeRegistry instance;
        return instance;
    }

    void RegisterType(const std::string &typeName, const TypeInfo &typeInfo)
    {
        types[typeName] = typeInfo;
    }

    TypeInfo *GetType(const std::string &typeName)
    {
        auto it = types.find(typeName);
        return (it != types.end()) ? &it->second : nullptr;
    }

private:
    std::unordered_map<std::string, TypeInfo> types;
};
#define ACLASS()
#define PROPERTY()
#define FUNCTION()

#define REGISTER_TYPE(TYPE) \
    TypeRegistry::Instance().RegisterType(#TYPE, TYPE::GetTypeInfo());

#define REGISTER_FIELD(TYPE, FIELD)                                                  \
    {#FIELD,                                                                         \
     [](void *instance) -> void * { return &static_cast<TYPE *>(instance)->FIELD; }, \
     [](void *instance, void *value) { static_cast<TYPE *>(instance)->FIELD = *static_cast<decltype(TYPE::FIELD) *>(value); }}

#define REGISTER_METHOD(TYPE, METHOD) \
    {#METHOD, [](void *instance) { static_cast<TYPE *>(instance)->METHOD(); }}
