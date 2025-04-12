#pragma once
#ifndef __GFX_CONTEXT_H__
#define __GFX_CONTEXT_H__
#include "GfxCommand.h"
#include "GlobalMarco.h"
#include "GpuResource.h"
#include <functional>
namespace Ailu
{
    class CommandBuffer;
    class RHICommandBuffer;
    class GpuResource;
    class IGPUTimer;
    class RenderPipeline;
    class Shader;
    class ComputeShader;
    class AILU_API GraphicsContext
    {
        friend class GpuCommandWorker;
    public:
        static void InitGlobalContext();
        static void FinalizeGlobalContext();
        static GraphicsContext &Get();
        virtual ~GraphicsContext() = default;
        virtual void Init() = 0;
        virtual void Present() = 0;
        virtual u64 GetFenceValueGPU() const = 0;
        virtual u64 GetFenceValueCPU() const = 0;
        /// @brief 提交命令，帧结束时执行或者渲染线程异步执行
        /// @param cmd 
        virtual void ExecuteCommandBuffer(Ref<CommandBuffer> &cmd) = 0;
        /// @brief 在主线程执行立即命令
        /// @param cmd 
        virtual void ExecuteCommandBufferSync(Ref<CommandBuffer> &cmd) = 0;
        virtual void WaitForGpu() = 0;
        virtual void WaitForFence(u64 fence_value) = 0;
        virtual u64 GetFrameCount() const = 0;
        virtual void CreateResource(GpuResource* res) = 0;
        virtual void CreateResource(GpuResource* res,UploadParams* params) = 0;
        virtual void ReadBack(GpuResource* res,u8* data,u32 size) = 0;
        virtual void ReadBackAsync(GpuResource* res,std::function<void(u8*)> callback) = 0;
        virtual void ProcessGpuCommand(GfxCommand * cmd,RHICommandBuffer* cmd_buffer) = 0;
        /// 在主线程提交一个立即执行的 gfx command
        virtual void SubmitGpuCommandSync(GfxCommand * cmd) = 0;
        /// @brief 提交一个shader异步编译任务
        /// @param obj 
        virtual void CompileShaderAsync(Shader* shader) = 0;
        /// @brief 提交一个compute shader异步编译任务
        /// @param obj 
        virtual void CompileShaderAsync(ComputeShader* shader) = 0;

        virtual void TakeCapture() = 0;
        virtual void ResizeSwapChain(const u32 width, const u32 height) = 0;
        virtual std::tuple<u32, u32> GetSwapChainSize() const = 0;
        virtual IGPUTimer *GetTimer() = 0;
        virtual const u32 CurBackbufIndex() const = 0;
        virtual void TryReleaseUnusedResources() = 0;
        //return mb byte/1024/1024
        virtual f32 TotalGPUMemeryUsage() = 0;
        virtual RenderPipeline *GetPipeline() = 0;
        virtual void RegisterPipeline(RenderPipeline *pipiline) = 0;
    protected:
        virtual void ExecuteRHICommandBuffer(RHICommandBuffer* cmd) = 0;
    };
    extern AILU_API GraphicsContext *g_pGfxContext;
}// namespace Ailu

#endif// !GFX_CONTEXT_H__
