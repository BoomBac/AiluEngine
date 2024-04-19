#pragma once
#ifndef __GFX_CONTEXT_H__
#define __GFX_CONTEXT_H__
#include "GlobalMarco.h"

namespace Ailu
{
	class CommandBuffer;
	class GraphicsContext
	{
	public:
		using RHIResourceTask = std::function<void()>;
		static void InitGlobalContext();
		static void FinalizeGlobalContext();
		virtual ~GraphicsContext() = default;
		virtual void Init() = 0;
		virtual void Present() = 0;
		//It will move the cmd in cmd buffer
		virtual const u64& GetFenceValue(const u32& cmd_index) const = 0;
		virtual u64 GetCurFenceValue() const = 0;
		virtual u64 ExecuteCommandBuffer(Ref<CommandBuffer>& cmd) = 0;
		virtual void SubmitRHIResourceBuildTask(RHIResourceTask task) = 0;
		virtual void TakeCapture() = 0;
	};
	extern GraphicsContext* g_pGfxContext;
}

#endif // !GFX_CONTEXT_H__

