#pragma warning(disable : 4251)
#pragma once
#ifndef __OBJECT_H__
#define __OBJECT_H__
#include "Framework/Common/Reflect.h"
#include "Framework/Math/Guid.h"
#include <ranges>
#include "generated/Object.gen.h"

namespace Ailu
{
    class Type;
    ACLASS()
    class AILU_API Object
    {
        GENERATED_BODY();
        friend class ResourceMgr;
    public:
        Object();
        explicit Object(const String &name);
        //Object &operator=(const Object &other);
        //Object &operator=(Object &&other) noexcept;
        bool operator==(const Object &other) const { return _id == other._id; };
        bool operator<(const Object &other) const { return _id < other._id; };
        virtual void Name(const String &value) { _name = value; }
        [[nodiscard]] const String &Name() const { return _name; }
//        virtual Type *GetType();
//        virtual String ToString();
        void ID(const u32 &value) { _id = value; }
        [[nodiscard]] const u32 &ID() const { return _id; }
        [[nodiscard]] const u64 HashCode() const { return _hash; };
        [[nodiscard]] const Guid GetGuid() const { return _guid; };
    protected:
        APROPERTY()
        String _name;
        APROPERTY()
        u32 _id;
        APROPERTY()
        u64 _hash;
        APROPERTY()
        Guid _guid;
    private:
        //0~64 reserve for shader, shader id hash only hash 6bit
        inline static u32 s_global_object_id = 65u;
    };
}// namespace Ailu


#endif// !OBJECT_H__
