//Generated by ahl
#include "../Camera.h"
#include <memory>
#include <Inc/Objects/Type.h>
using namespace Ailu;
//Enum EAntiAliasing begin...........................
void Z_Construct_Enum_EAntiAliasing_Type()
{
static std::unique_ptr<Ailu::Enum> cur_type = nullptr;
if(cur_type == nullptr)
{
EnumInitializer initializer;
initializer._name = "EAntiAliasing";
initializer._str_to_enum_lut["kNone"] = 0;
initializer._str_to_enum_lut["kFXAA"] = 1;
initializer._str_to_enum_lut["kTAA"] = 2;
cur_type = std::make_unique<Ailu::Enum>(initializer);
Ailu::Enum::RegisterEnum(cur_type.get());
}
}
//Enum EAntiAliasing end...........................

