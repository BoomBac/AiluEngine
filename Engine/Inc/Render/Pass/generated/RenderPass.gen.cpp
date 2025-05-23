//Generated by ahl
#include "../RenderPass.h"
#include <memory>
#include <Inc/Objects/Type.h>
using namespace Ailu;
Ailu::Type* Ailu::Z_Construct_RenderPass_Type()
{
static std::unique_ptr<Ailu::Type> cur_type = nullptr;
if(cur_type == nullptr)
{
TypeInitializer initializer;
initializer._name = "RenderPass";
initializer._size = sizeof(RenderPass);
initializer._full_name = "Ailu::RenderPass";
initializer._is_class = true;
initializer._is_abstract = false;
initializer._namespace = "Ailu";
initializer._base_name = "Ailu::IRenderPass";
cur_type = std::make_unique<Ailu::Type>(initializer);
Ailu::Type::RegisterType(cur_type.get());
}
return cur_type.get();
}

Ailu::Type* RenderPass::GetPrivateStaticClass()
{
	static Ailu::Type* type = Z_Construct_RenderPass_Type();
	return type;
}

    const Type *RenderPass::GetType() const
{
return RenderPass::GetPrivateStaticClass();
}
