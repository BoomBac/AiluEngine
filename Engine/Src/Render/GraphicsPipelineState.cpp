#include "pch.h"
#include "Render/Renderer.h"
#include "Render/GraphicsPipelineState.h"
#include "RHI/DX12/D3DGraphicsPipelineState.h"

namespace Ailu
{
	GraphicsPipelineState* GraphicsPipelineState::Create()
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::ERenderAPI::kNone:
			AL_ASSERT(false, "None render api used!");
			return nullptr;
		case RendererAPI::ERenderAPI::kDirectX12:
		{
			s_pipelinestates.insert(std::make_pair(s_pso_index, std::make_unique<D3DGraphicsPipelineState>()));
			sCurrent = s_pipelinestates[s_pso_index++].get();
			return sCurrent;
		}
		}
		AL_ASSERT(false, "Unsupport render api!");
		return nullptr;
	}
}

