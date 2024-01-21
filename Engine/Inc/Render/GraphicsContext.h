#pragma once
#ifndef __GFX_CONTEXT_H__
#define __GFX_CONTEXT_H__
#include "GlobalMarco.h"
#include "CommandBuffer.h"
#include "CBuffer.h"

namespace Ailu
{
	class GraphicsContext
	{
	public:
		static void InitGlobalContext();
		virtual ~GraphicsContext() = default;
		virtual void Init() = 0;
		virtual void Present() = 0;
		virtual uint8_t* GetPerFrameCbufData() = 0;
		virtual ScenePerFrameData* GetPerFrameCbufDataStruct() = 0;
		virtual uint8_t* GetPerMaterialCbufData(uint32_t mat_index) = 0;
		virtual void* GetCBufGPURes(u32 index) = 0;
		virtual void* GetCBufferPerPassGPUPtr(u16 index) = 0;
		virtual u8* GetCBufferPerPassCPUPtr(u16 index) = 0;
		//It will move the cmd in cmd buffer
		virtual void ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) = 0;
	};
	extern Ref<GraphicsContext> g_pGfxContext;
}

#endif // !GFX_CONTEXT_H__

