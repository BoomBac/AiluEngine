#include "Test.h"

static TypeInfo GetPersonTypeInfo()
{
    TypeInfo typeInfo;
    typeInfo.typeName = "Person";
    typeInfo.fields["name"] = REGISTER_FIELD(Person, name);
    typeInfo.fields["age"] = REGISTER_FIELD(Person, age);
    return typeInfo;
}