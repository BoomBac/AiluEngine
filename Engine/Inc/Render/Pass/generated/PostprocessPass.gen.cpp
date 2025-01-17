//Generated by ahl
#include "../PostprocessPass.h"
#include <memory>
#include <Inc/Objects/Type.h>
using namespace Ailu;
Ailu::Type* Ailu::Z_Construct_PostProcessPass_Type()
{
static std::unique_ptr<Ailu::Type> cur_type = nullptr;
if(cur_type == nullptr)
{
TypeInitializer initializer;
initializer._name = "PostProcessPass";
initializer._size = sizeof(PostProcessPass);
initializer._full_name = "Ailu::PostProcessPass";
initializer._is_class = true;
initializer._is_abstract = false;
initializer._namespace = "Ailu";
initializer._properties.emplace_back(MemberInfoInitializer(EMemberType::kProperty,"_upsample_radius","f32", false, true,0,&PostProcessPass::_upsample_radius));
initializer._properties.emplace_back(MemberInfoInitializer(EMemberType::kProperty,"_bloom_intensity","f32", false, true,0,&PostProcessPass::_bloom_intensity));
initializer._properties.emplace_back(MemberInfoInitializer(EMemberType::kProperty,"_is_use_blur","bool", false, true,0,&PostProcessPass::_is_use_blur));
cur_type = std::make_unique<Ailu::Type>(initializer);
}
return cur_type.get();
}

Ailu::Type* PostProcessPass::GetPrivateStaticClass()
{
	static Ailu::Type* type = Z_Construct_PostProcessPass_Type();
	return type;
}

