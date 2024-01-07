#include "pch.h"
#include "Render/Shader.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DShader.h"
#include "Render/GraphicsPipelineStateObject.h"
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
			std::string  shader_name = PathUtils::GetFileName(file_name);
			auto shader = MakeRef<D3DShader>(file_name, shader_name, ShaderLibrary::s_shader_id++);
			ShaderLibrary::Add(shader_name, shader);
			GraphicsPipelineStateInitializer gpso_desc;
			gpso_desc._input_layout = shader->PipelineInputLayout();
			gpso_desc._blend_state = shader->PipelineBlendState();
			gpso_desc._raster_state = shader->PipelineRasterizerState();
			gpso_desc._depth_stencil_state = shader->PipelineDepthStencilState();
			gpso_desc._topology = shader->PipelineTopology();
			gpso_desc._p_pixel_shader = shader;
			gpso_desc._p_vertex_shader = shader;
			gpso_desc._rt_state = RenderTargetState{};
			auto pso = GraphicsPipelineStateObject::Create(gpso_desc);
			pso->Build();
			GraphicsPipelineStateMgr::Register(shader->PipelineInputLayout());
			GraphicsPipelineStateMgr::Register(shader->PipelineBlendState());
			GraphicsPipelineStateMgr::Register(shader->PipelineRasterizerState());
			GraphicsPipelineStateMgr::Register(shader->PipelineDepthStencilState());
			GraphicsPipelineStateMgr::Register(shader->PipelineTopology());

			LOG_WARNING("shader: {},hash code is {}",file_name, pso->Hash().ToString());
			GraphicsPipelineStateMgr::AddPSO(std::move(pso));
			return shader;
		}
		}
		AL_ASSERT(false, "Unsupport render api!")
			return nullptr;
	}
}
