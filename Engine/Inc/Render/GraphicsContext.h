#pragma once
#ifndef __GFX_CONTEXT_H__
#define __GFX_CONTEXT_H__
#include "GlobalMarco.h"

namespace Ailu
{
    class CommandBuffer;
    class FrameResource;
    class IGPUTimer;
    class RenderPipeline;
    class AILU_API GraphicsContext
    {
    public:
        using RHIResourceTask = std::function<void()>;
        static void InitGlobalContext();
        static void FinalizeGlobalContext();
        virtual ~GraphicsContext() = default;
        virtual void Init() = 0;
        virtual void Present() = 0;
        //It will move the cmd in cmd buffer
        virtual const u64 &GetFenceValue(const u32 &cmd_index) const = 0;
        virtual u64 GetCurFenceValue() const = 0;
        virtual u64 ExecuteCommandBuffer(Ref<CommandBuffer> &cmd) = 0;
        virtual u64 ExecuteAndWaitCommandBuffer(Ref<CommandBuffer> &cmd) = 0;
        virtual void WaitForGpu() = 0;
        virtual u64 GetFrameCount() const = 0;

        virtual void BeginBackBuffer(CommandBuffer *cmd) = 0;
        virtual void EndBackBuffer(CommandBuffer *cmd) = 0;
        virtual void DrawOverlay(CommandBuffer *cmd) = 0;

        virtual bool IsCommandBufferReady(const u32 cmd_index) = 0;
        virtual void SubmitRHIResourceBuildTask(RHIResourceTask task) = 0;
        virtual void TakeCapture() = 0;
        virtual void TrackResource(FrameResource *resource) = 0;
        virtual bool IsResourceReferencedByGPU(FrameResource *resource) = 0;
        virtual void ResizeSwapChain(const u32 width, const u32 height) = 0;
        virtual std::tuple<u32, u32> GetSwapChainSize() const = 0;
        virtual IGPUTimer *GetTimer() = 0;
        virtual const u32 CurBackbufIndex() const = 0;
        virtual void TryReleaseUnusedResources() = 0;
        //return mb byte/1024/1024
        virtual f32 TotalGPUMemeryUsage() = 0;
        virtual RenderPipeline *GetPipeline() = 0;
        virtual void RegisterPipeline(RenderPipeline *pipiline) = 0;
    };
    extern AILU_API GraphicsContext *g_pGfxContext;
}// namespace Ailu

#endif// !GFX_CONTEXT_H__
