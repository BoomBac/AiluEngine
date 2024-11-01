#include "Test.gen.h"
static void InitReflection()
{
    TypeRegistry::Instance().RegisterType("Person", GetPersonTypeInfo());
}