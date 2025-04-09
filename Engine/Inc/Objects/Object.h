#pragma warning(disable : 4251)
#pragma once
#ifndef __OBJECT_H__
#define __OBJECT_H__
#include <set>
#include <mutex>
#include "Framework/Math/Guid.h"
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
        virtual ~Object();
        virtual void Name(const String &value) { _name = value; }
        [[nodiscard]] const String &Name() const { return _name; }
        bool operator==(const Object &other) const { return _id == other._id; };
        bool operator<(const Object &other) const { return _id < other._id; };
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
    struct ObjectPtrCompare
    {
        bool operator()(Object* lhs, Object* rhs) const {
            return *lhs < *rhs;
        }
    };

    class AILU_API ObjectRegister
    {
    public:
        static void Initialize();
        static void Shutdown();
        static ObjectRegister& Get();
    public:
        using ObjectSet = std::set<Object*,ObjectPtrCompare>;
        ObjectRegister() = default;
        void Register(Object* object);
        void Unregister(Object* object);
        bool Alive(Object* obj) const;
        Object* Find(const u32 &id);
        void AddReference(Object* parent, Object* child);
        void RemoveReference(Object* parent, Object* child);
        const ObjectSet& GetReferences(Object* object) const;
        const ObjectSet& GetReferencesBy(Object* object) const;
    private:
        std::mutex _mutex;
        std::set<Object*> _global_register;
        HashMap<u32,Object*> _id_to_obj_lut;
        HashMap<u32,ObjectSet> _global_references;
        HashMap<u32,ObjectSet> _global_references_by;
    };
}// namespace Ailu


#endif// !OBJECT_H__
