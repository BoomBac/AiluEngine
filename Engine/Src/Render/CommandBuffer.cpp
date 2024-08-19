#include "Render/CommandBuffer.h"
#include "Framework/Common/ThreadPool.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"
#include "pch.h"

namespace Ailu
{
    std::shared_ptr<CommandBuffer> CommandBufferPool::Get(const String &name, ECommandBufferType type)
    {
        static bool s_b_init = false;
        if (!s_b_init)
        {
            Init();
            s_b_init = true;
        }
        for (auto &cmd: s_cmd_buffers)
        {
            auto &[available, cmd_buf] = cmd;

            if (available && type == cmd_buf->GetType())
            {
                if (g_pGfxContext->IsCommandBufferReady(cmd_buf->GetID()))
                {
                    available = false;
                    cmd_buf->Clear();
                    cmd_buf->SetName(name);
                    return cmd_buf;
                }
            }
            //else
            //{
            //	if (g_pGfxContext->GetFenceValue(cmd_buf->GetID()) < g_pGfxContext->GetCurFenceValue())
            //	{
            //		LOG_WARNING("cmd {} is ready cmd value {},gpu value {}!", cmd_buf->GetID(), g_pGfxContext->GetFenceValue(cmd_buf->GetID()), g_pGfxContext->GetCurFenceValue());
            //		cmd_buf->Clear();
            //		cmd_buf->SetName(name);
            //		return cmd_buf;
            //	}
            //}
        }
        std::unique_lock<std::mutex> lock(_mutex);
        if (Renderer::GetAPI() == RendererAPI::ERenderAPI::kDirectX12)
        {
            int newId = (u32) s_cmd_buffers.size();
            auto cmd = std::make_shared<D3DCommandBuffer>(newId, type);
            s_cmd_buffers.emplace_back(std::make_tuple(false, cmd));
            ++s_cur_pool_size;
            LOG_WARNING("Expand commandbuffer pool to {}", s_cur_pool_size);
            cmd->SetName(name);
            cmd->Clear();
            return cmd;
        }
        else
            AL_ASSERT_MSG(true, "None render api used!");
    }

    void CommandBufferPool::Release(std::shared_ptr<CommandBuffer> &cmd)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::get<0>(s_cmd_buffers[cmd->GetID()]) = true;
        //cmd->Clear();
    }

    void CommandBufferPool::ReleaseAll()
    {
        //std::unique_lock<std::mutex> lock(_mutex);
        //for (auto& it : s_cmd_buffers)
        //{
        //    auto& [available, cmd] = it;
        //    available = true;
        //    cmd->Clear();
        //}
    }

    void CommandBufferPool::Init()
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::ERenderAPI::kNone:
                AL_ASSERT_MSG(false, "None render api used!");
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
        AL_ASSERT_MSG(false, "Unsupport render api!");
        return;
    }
    void CommandBufferPool::WaitForAllCommand()
    {
        while (true)
        {
            u32 total_count = 0;
            u32 completed_count = 0;
            for (auto &cmd: s_cmd_buffers)
            {
                auto &[available, cmd_buf] = cmd;
                if (available)
                {
                    ++total_count;
                    if (g_pGfxContext->IsCommandBufferReady(cmd_buf->GetID()))
                    {
                        ++completed_count;
                    }
                }
            }
            if (total_count == completed_count)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}// namespace Ailu
