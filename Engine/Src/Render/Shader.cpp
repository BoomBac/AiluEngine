#include "pch.h"
#include "Render/Shader.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DShader.h"

namespace Ailu
{

	Shader* Shader::Create(const std::string_view file_name, EShaderType type)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
			return new D3DShader(file_name,type);
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
}
