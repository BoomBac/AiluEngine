#include "TypeInfo.h"

class Person
{
    ACLASS()
public:
    PROPERTY()
    std::string name;
    PROPERTY()
    int age;
};

class Child
{
    ACLASS()
public:
    PROPERTY()
    std::string name;
};