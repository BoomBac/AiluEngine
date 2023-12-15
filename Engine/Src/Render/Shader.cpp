#include "pch.h"
#include "Render/Shader.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DShader.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"


namespace Ailu
{
	Ref<Shader> Shader::Create(const std::string& file_name)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			std::string  shader_name = GetFileName(file_name);
			auto shader = MakeRef<D3DShader>(file_name, shader_name, ShaderLibrary::s_shader_id++);
			ShaderLibrary::Add(shader_name, shader);
			return shader;
		}
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
}
