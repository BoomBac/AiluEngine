//
// Created by 22292 on 2025/3/17.
//
#include "Render/GfxCommand.h"
#include "pch.h"

namespace Ailu::Render
{
    static CommandPool *g_pCommandPool = nullptr;
    CommandPool &CommandPool::Get()
    {
        return *g_pCommandPool;
    }
    void CommandPool::Init()
    {
        if (g_pCommandPool == nullptr)
        {
            g_pCommandPool = new CommandPool();
            for (u32 i = 0; i < g_pCommandPool->_pool_set_target.Capacity(); i++)
                g_pCommandPool->_pool_set_target.Push(AL_NEW(CommandSetTarget));
            for (u32 i = 0; i < g_pCommandPool->_pool_clear_target.Capacity(); i++)
                g_pCommandPool->_pool_clear_target.Push(AL_NEW(CommandClearTarget));
            for (u32 i = 0; i < g_pCommandPool->_pool_draw.Capacity(); i++)
                g_pCommandPool->_pool_draw.Push(AL_NEW(CommandDraw));
            for (u32 i = 0; i < g_pCommandPool->_pool_dispatch.Capacity(); i++)
                g_pCommandPool->_pool_dispatch.Push(AL_NEW(CommandDispatch));
            for (u32 i = 0; i < g_pCommandPool->_pool_resource_upload.Capacity(); i++)
                g_pCommandPool->_pool_resource_upload.Push(AL_NEW(CommandGpuResourceUpload));
            for (u32 i = 0; i < g_pCommandPool->_pool_resource_translate.Capacity(); i++)
                g_pCommandPool->_pool_resource_translate.Push(AL_NEW(CommandTranslateState));
            for (u32 i = 0; i < g_pCommandPool->_pool_custom.Capacity(); i++)
                g_pCommandPool->_pool_custom.Push(AL_NEW(CommandCustom));
            for (u32 i = 0; i < g_pCommandPool->_pool_alloc_const_buffer.Capacity(); i++)
                g_pCommandPool->_pool_alloc_const_buffer.Push(AL_NEW(CommandAllocConstBuffer));
            for (u32 i = 0; i < g_pCommandPool->_pool_profiler.Capacity(); i++)
                g_pCommandPool->_pool_profiler.Push(AL_NEW(CommandProfiler));
            for (u32 i = 0; i < g_pCommandPool->_pool_present.Capacity(); i++)
                g_pCommandPool->_pool_present.Push(AL_NEW(CommandPresent));
            for (u32 i = 0; i < g_pCommandPool->_pool_cp_counter.Capacity(); i++)
                g_pCommandPool->_pool_cp_counter.Push(AL_NEW(CommandCopyCounter));
        }
    }
    void CommandPool::Shutdown()
    {
        for (u32 i = 0; i < g_pCommandPool->_pool_set_target.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_set_target.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_clear_target.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_clear_target.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_draw.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_draw.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_dispatch.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_dispatch.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_resource_upload.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_resource_upload.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_resource_translate.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_resource_translate.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_custom.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_custom.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_alloc_const_buffer.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_alloc_const_buffer.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_profiler.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_profiler.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_present.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_present.Pop().value());
        for (u32 i = 0; i < g_pCommandPool->_pool_cp_counter.Capacity(); i++)
            AL_DELETE(g_pCommandPool->_pool_cp_counter.Pop().value());
    }
}// namespace Ailu