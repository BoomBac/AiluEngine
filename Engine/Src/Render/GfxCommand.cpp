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
        case EGpuCommandType::kRequireResourceState:
            return "kRequireResourceState";
        case EGpuCommandType::kUavBarrier:
            return "kUavBarrier";
        case EGpuCommandType::kAllocConstBuffer:
            return "kAllocConstBuffer";
        case EGpuCommandType::kCommandProfiler:
            return "kCommandProfiler";
        case EGpuCommandType::kCopyCounter:
            return "kCopyCounter";
        case EGpuCommandType::kReadBack:
            return "kReadBack";
        case EGpuCommandType::kReadBackTexture:
            return "kReadBackTexture";
        case EGpuCommandType::kPresent:
            return "kPresent";
        case EGpuCommandType::kEndFrame:
            return "kEndFrame";
        case EGpuCommandType::kScissorRect:
            return "kScissorRect";
        case EGpuCommandType::kDestroyGpuResource:
            return "kDestroyGpuResource";
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
        case EGpuCommandType::kRequireResourceState:
            static_cast<CommandRequireResourceState *>(cmd)->~CommandRequireResourceState();
            break;
        case EGpuCommandType::kUavBarrier:
            static_cast<CommandUavBarrier *>(cmd)->~CommandUavBarrier();
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
        case EGpuCommandType::kReadBackTexture:
            static_cast<CommandReadBackTexture *>(cmd)->~CommandReadBackTexture();
            break;
        case EGpuCommandType::kPresent:
            static_cast<CommandPresent *>(cmd)->~CommandPresent();
            break;
        case EGpuCommandType::kEndFrame:
            static_cast<CommandEndFrame *>(cmd)->~CommandEndFrame();
            break;
        case EGpuCommandType::kScissorRect:
            static_cast<CommandScissor *>(cmd)->~CommandScissor();
            break;
        case EGpuCommandType::kDestroyGpuResource:
            static_cast<CommandDestroyGpuResource *>(cmd)->~CommandDestroyGpuResource();
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
            auto command_pool = AL_NEW_TAG(EMemoryTag::kRenderer, CommandPool);
            if (command_pool == nullptr)
                throw std::bad_alloc();
            g_pCommandPool = command_pool;
            g_pCommandPool->_owned_payloads.reserve(kCommandPoolPayloadCount);
            for (u32 i = 0; i < kCommandPoolPayloadCount; ++i)
            {
                auto payload = AL_NEW(CommandPayload);
                if (payload == nullptr)
                    throw std::bad_alloc();
                g_pCommandPool->_owned_payloads.push_back(payload);
                g_pCommandPool->_payload_pool.Push(payload);
            }
        }
    }

    CommandPayload *CommandPool::AcquirePayload()
    {
        auto payload = _payload_pool.Pop().value_or(nullptr);
        if (payload == nullptr)
        {
            std::lock_guard lock(_growth_mutex);
            payload = _payload_pool.Pop().value_or(nullptr);
            if (payload == nullptr)
            {
                _owned_payloads.reserve(_owned_payloads.size() + kCommandPoolPayloadGrowthCount);
                for (u32 i = 0; i < kCommandPoolPayloadGrowthCount; ++i)
                {
                    auto new_payload = AL_NEW(CommandPayload);
                    if (new_payload == nullptr)
                        break;

                    _owned_payloads.push_back(new_payload);
                    if (payload == nullptr)
                        payload = new_payload;
                    else
                        _payload_pool.Push(new_payload);
                }

                if (payload == nullptr)
                {
                    LOG_ERROR("Failed to grow GPU command payload pool; allocation returned null");
                    throw std::bad_alloc();
                }

                LOG_WARNING("Expanded GPU command payload pool to {} payloads", _owned_payloads.size());
            }
        }

        payload->_in_use.store(true, std::memory_order_release);
        return payload;
    }

    void CommandPool::Shutdown()
    {
        if (g_pCommandPool == nullptr)
            return;

        for (auto &payload : g_pCommandPool->_owned_payloads)
        {
            if (payload->_in_use.load(std::memory_order_acquire))
                DestroyCommand(static_cast<GfxCommand *>(static_cast<void *>(payload->Data())));
            AL_DELETE(payload);
        }

        AL_DELETE(g_pCommandPool);
        g_pCommandPool = nullptr;
    }
}// namespace Ailu
