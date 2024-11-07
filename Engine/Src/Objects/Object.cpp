#include "Objects/Object.h"
#include "pch.h"

namespace Ailu
{

    Object::Object()
    {
        _name = std::format("object_{}", s_global_object_id);
        _id = s_global_object_id++;
        _hash = _id;
    }
    Object::Object(const String &name) : Object()
    {
        _name = name;
    }

//    Object &Object::operator=(const Object &other)
//    {
//        _name = other._name;
//        _id = other._id;
//        _hash = other._hash;
//        return *this;
//    }
//
//    Object &Object::operator=(Object &&other) noexcept
//    {
//        _name = other._name;
//        _id = other._id;
//        _hash = other._hash;
//        other._name.clear();
//        other._id = -1;
//        return *this;
//    }
}// namespace Ailu
