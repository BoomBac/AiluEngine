#include "pch.h"
#include "Render/Shader.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DShader.h"


namespace Ailu
{
	Shader* Shader::Create(const std::string_view file_name, const std::string& shader_name, EShaderType type)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!")
				return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			auto shader = new D3DShader(file_name, shader_name, ShaderLibrary::s_shader_id++,type);
			ShaderLibrary::s_shader_name.insert(std::make_pair(shader_name, shader->GetID()));
			ShaderLibrary::s_shader_library.insert(std::make_pair(shader->GetID(), shader));
			return shader;
		}
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
	Shader* Shader::Create(const std::string_view file_name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			std::string  shader_name = GetFileName(file_name);
			auto shader = new D3DShader(file_name, shader_name, ShaderLibrary::s_shader_id++);
			ShaderLibrary::s_shader_name.insert(std::make_pair(shader_name, shader->GetID()));
			ShaderLibrary::s_shader_library.insert(std::make_pair(shader->GetID(), shader));
			return shader;
		}
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
}
