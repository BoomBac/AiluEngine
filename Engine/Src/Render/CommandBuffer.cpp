#include "pch.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"
#include "RHI/DX12/D3DCommandBuffer.h"

namespace Ailu
{
	std::shared_ptr<CommandBuffer> CommandBufferPool::GetCommandBuffer()
    {
        static bool s_b_init = false;
        if (!s_b_init)
        {
            Init();
            s_b_init = true;
        }
        std::unique_lock<std::mutex> lock(_mutex);
        for (auto& cmd : s_cmd_buffers)
        {
            auto& [avairable, cmd_buf] = cmd;
            if (avairable) 
            {
                avairable = false;
                return cmd_buf;
            }
        }
        if (Renderer::GetAPI() == RendererAPI::ERenderAPI::kDirectX12)
        {
            int newId = (uint32_t)s_cmd_buffers.size();
            auto cmd = std::make_shared<D3DCommandBuffer>(newId);
            s_cmd_buffers.emplace_back(std::make_tuple(false, cmd));
            return cmd;
        }
        else
            AL_ASSERT(true,"None render api used!")
    }

    void CommandBufferPool::ReleaseCommandBuffer(std::shared_ptr<CommandBuffer> cmd)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::get<0>(s_cmd_buffers[cmd->GetID()]) = true;
        cmd->Clear();
    }

    void CommandBufferPool::Init()
    {
        switch (Renderer::GetAPI())
        {
        case RendererAPI::ERenderAPI::kNone:
            AL_ASSERT(false, "None render api used!");
            return;
        case RendererAPI::ERenderAPI::kDirectX12:
        {
            for (int i = 0; i < kInitialPoolSize; i++)
            {
                auto cmd = std::make_shared<D3DCommandBuffer>(i);
                s_cmd_buffers.emplace_back(std::make_tuple(true, cmd));
            }
        }
        }
        AL_ASSERT(false, "Unsupport render api!")
            return;
    }
}
