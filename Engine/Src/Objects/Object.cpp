#include "Objects/Object.h"
#include "Framework/Common/Log.h"
#include "pch.h"

namespace Ailu
{
    #pragma region Object
    Object::Object() : _id(s_global_object_id++)
    {
        _name = std::format("object_{}", s_global_object_id);
        _hash = _id;
        ObjectRegister::Get().Register(this);
    }
    Object::Object(const String &name) : Object()
    {
        _name = name;
    }

    //
    Object::~Object()
    {
        ObjectRegister::Get().Unregister(this);
    }

    #pragma endregion

    #pragma region ObjectRegister
    static ObjectRegister* g_pObjectRegister = nullptr;
    void ObjectRegister::Initialize()
    {
        //g_pObjectRegister = new ObjectRegister();
    }

    void ObjectRegister::Shutdown()
    {
        DESTORY_PTR(g_pObjectRegister);
    }

    ObjectRegister &ObjectRegister::Get()
    {
        if (g_pObjectRegister == nullptr)
            g_pObjectRegister = new ObjectRegister();
        return *g_pObjectRegister;
    }

    void ObjectRegister::Register(Object *object)
    {
        std::unique_lock lock(_mutex);
        u32 id = object->ID();
        if (!_global_register.contains(object))
        {
            _global_register.insert(object);
            _id_to_obj_lut[id] = object;
            _global_references[id] = ObjectSet{};
            _global_references_by[id] = ObjectSet{};
        }
    }

    void ObjectRegister::Unregister(Object *object)
    {
        std::unique_lock lock(_mutex);
        u32 id = object->ID();
        if (_global_register.contains(object))
        {
            _global_register.erase(object);
            _id_to_obj_lut.erase(id);
            _global_references.erase(id);
            _global_references_by.erase(id);
        }
    }
    Object *ObjectRegister::Find(const u32 &id)
    {
        if (_id_to_obj_lut.contains(id))
            return _id_to_obj_lut[id];
        return nullptr;
    }
    bool ObjectRegister::Alive(Object* obj) const
    {
        return _global_register.contains(obj);
    }

    void ObjectRegister::AddReference(Object *parent, Object *child)
    {
        _global_references.at(parent->ID()).insert(child);
        _global_references_by.at(child->ID()).insert(parent);
    }
    void ObjectRegister::RemoveReference(Object * parent, Object * child)
    {
        _global_references.at(parent->ID()).erase(child);
        _global_references_by.at(child->ID()).erase(parent);
    }

    const ObjectRegister::ObjectSet& ObjectRegister::GetReferences(Object *object) const
    {
        return _global_references.at(object->ID());
    }

    const ObjectRegister::ObjectSet& ObjectRegister::GetReferencesBy(Object *object) const
    {
        return _global_references_by.at(object->ID());
    }


#pragma endregion
}// namespace Ailu

