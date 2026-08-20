//
// Created by 22292 on 2025/3/17.
//
#include "Render/GfxCommand.h"
#include "pch.h"

namespace Ailu::Render
{
    const char *GfxCommandTypeName(EGpuCommandType type)
    {
        switch (type)
        {
        case EGpuCommandType::kSetTarget:
            return "kSetTarget";
        case EGpuCommandType::kClearTarget:
            return "kClearTarget";
        case EGpuCommandType::kDraw:
            return "kDraw";
        case EGpuCommandType::kDispatch:
            return "kDispatch";
        case EGpuCommandType::kResourceUpload:
            return "kResourceUpload";
        case EGpuCommandType::kTransResourceState:
            return "kTransResourceState";
        case EGpuCommandType::kResourceBarrier:
            return "kResourceBarrier";
        case EGpuCommandType::kUAVBarrier:
            return "kUAVBarrier";
        case EGpuCommandType::kAllocConstBuffer:
            return "kAllocConstBuffer";
        case EGpuCommandType::kCommandProfiler:
            return "kCommandProfiler";
        case EGpuCommandType::kCopyCounter:
            return "kCopyCounter";
        case EGpuCommandType::kReadBack:
            return "kReadBack";
        case EGpuCommandType::kPresent:
            return "kPresent";
        case EGpuCommandType::kScissorRect:
            return "kScissorRect";
        case EGpuCommandType::kCustom:
            return "kCustom";
        default:
            return "Unknown";
        }
    }

    void DestroyCommand(GfxCommand *cmd)
    {
        switch (cmd->GetCmdType())
        {
        case EGpuCommandType::kSetTarget:
            static_cast<CommandSetTarget *>(cmd)->~CommandSetTarget();
            break;
        case EGpuCommandType::kClearTarget:
            static_cast<CommandClearTarget *>(cmd)->~CommandClearTarget();
            break;
        case EGpuCommandType::kDraw:
            static_cast<CommandDraw *>(cmd)->~CommandDraw();
            break;
        case EGpuCommandType::kDispatch:
            static_cast<CommandDispatch *>(cmd)->~CommandDispatch();
            break;
        case EGpuCommandType::kResourceUpload:
            static_cast<CommandGpuResourceUpload *>(cmd)->~CommandGpuResourceUpload();
            break;
        case EGpuCommandType::kTransResourceState:
            static_cast<CommandTranslateState *>(cmd)->~CommandTranslateState();
            break;
        case EGpuCommandType::kResourceBarrier:
            static_cast<CommandResourceBarrier *>(cmd)->~CommandResourceBarrier();
            break;
        case EGpuCommandType::kUAVBarrier:
            static_cast<CommandUAVBarrier *>(cmd)->~CommandUAVBarrier();
            break;
        case EGpuCommandType::kAllocConstBuffer:
            static_cast<CommandAllocConstBuffer *>(cmd)->~CommandAllocConstBuffer();
            break;
        case EGpuCommandType::kCommandProfiler:
            static_cast<CommandProfiler *>(cmd)->~CommandProfiler();
            break;
        case EGpuCommandType::kCopyCounter:
            static_cast<CommandCopyCounter *>(cmd)->~CommandCopyCounter();
            break;
        case EGpuCommandType::kReadBack:
            static_cast<CommandReadBack *>(cmd)->~CommandReadBack();
            break;
        case EGpuCommandType::kPresent:
            static_cast<CommandPresent *>(cmd)->~CommandPresent();
            break;
        case EGpuCommandType::kScissorRect:
            static_cast<CommandScissor *>(cmd)->~CommandScissor();
            break;
        case EGpuCommandType::kCustom:
            static_cast<CommandCustom *>(cmd)->~CommandCustom();
            break;
        default:
            break;
        }
    }
    
    static CommandPool *g_pCommandPool = nullptr;
    CommandPool &CommandPool::Get()
    {
        return *g_pCommandPool;
    }
    void CommandPool::Init()
    {
        if (g_pCommandPool == nullptr)
        {
            g_pCommandPool = AL_NEW_TAG(EMemoryTag::kRenderer, CommandPool);
            for (u32 i = 0; i < kCommandPoolPayloadCount; ++i)
                g_pCommandPool->_payload_pool.Push(AL_NEW(CommandPayload));
        }
    }
    void CommandPool::Shutdown()
    {
        if (g_pCommandPool == nullptr)
            return;

        while (auto payload = g_pCommandPool->_payload_pool.Pop())
        {
            AL_DELETE(payload.value());
        }

        AL_DELETE(g_pCommandPool);
    }
}// namespace Ailu
