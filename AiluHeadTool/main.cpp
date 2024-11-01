#include "TypeRegister.cpp"
#include <iostream>



void main()
{
    InitReflection();
	Person person;
    person.name = "Alice";
    person.age = 30;

    // 获取 Person 的类型信息
    TypeInfo *personType = TypeRegistry::Instance().GetType("Person");

    // 访问和修改成员变量
    FieldInfo *nameField = &personType->fields["name"];
    std::string *name = static_cast<std::string *>(nameField->getter(&person));
    std::cout << "Name: " << *name << std::endl;

    // 修改字段
    std::string newName = "Bob";
    nameField->setter(&person, &newName);
    std::cout << "Updated Name: " << person.name << std::endl;

	std::cout << "Hello World!" << std::endl;
}