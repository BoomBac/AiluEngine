#pragma once
#ifndef __GFX_CONTEXT_H__
#define __GFX_CONTEXT_H__
#include "GlobalMarco.h"
#include "CommandBuffer.h"
namespace Ailu
{
	class GraphicsContext
	{
	public:
		virtual ~GraphicsContext() = default;
		virtual void Init() = 0;
		virtual void Present() = 0;
		virtual uint8_t* GetPerFrameCbufData() = 0;
		virtual uint8_t* GetPerMaterialCbufData(uint32_t mat_index) = 0;
		virtual void* GetCBufGPURes(u32 index) = 0;
		//It will move the cmd in cmd buffer
		virtual void ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) = 0;
	};
}

#endif // !GFX_CONTEXT_H__

