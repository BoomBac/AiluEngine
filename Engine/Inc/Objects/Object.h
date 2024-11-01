#pragma warning(disable : 4251)
#pragma once
#ifndef __OBJECT_H__
#define __OBJECT_H__
#include "Framework/Common/Reflect.h"
#include "Framework/Math/Guid.h"
#include <ranges>

namespace Ailu
{
    class AILU_API Object
    {
        friend class ResourceMgr;
        //--------------------Reflect
    protected:
        std::unordered_map<String, SerializableProperty> _properties{};

    public:
        template<typename T>
        T GetProperty(const String &name)
        {
            return *reinterpret_cast<T *>(_properties.find(name)->second._value_ptr);
        }
        SerializableProperty &GetProperty(const String &name)
        {
            static SerializableProperty null{};
            if (_properties.contains(name))
                return _properties.find(name)->second;
            else
                return null;
        }
        std::unordered_map<String, SerializableProperty> &GetAllProperties()
        {
            return _properties;
        }
        auto PropertyBegin()
        {
            return _properties.begin();
        }
        auto PropertyEnd()
        {
            return _properties.end();
        }
        //--------------------Reflect
    public:
        Object();
        explicit Object(const String &name);
        Object(const Object &other);
        Object(Object &&other) noexcept;
        Object &operator=(const Object &other);
        Object &operator=(Object &&other) noexcept;
        bool operator==(const Object &other) const { return _id == other._id; };
        bool operator<(const Object &other) const { return _id < other._id; };
        virtual void Name(const String &value) { _name = value; }
        [[nodiscard]] const String &Name() const { return _name; }
        void ID(const u32 &value) { _id = value; }
        [[nodiscard]] const u32 &ID() const { return _id; }
        [[nodiscard]] const u64 HashCode() const { return _hash; };
        [[nodiscard]] const Guid GetGuid() const { return _guid; };

    protected:
        String _name;
        u32 _id;
        u64 _hash;
        Guid _guid;

    private:
        //0~64 reserve for shader, shader id hash only hash 6bit
        inline static u32 s_global_object_id = 65u;
    };
}// namespace Ailu


#endif// !OBJECT_H__
