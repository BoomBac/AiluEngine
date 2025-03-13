#include "Render/CommandBuffer.h"
#include "Framework/Common/ThreadPool.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"
#include "pch.h"
#include <Framework/Common/Application.h>

namespace Ailu
{
    static CommandBufferPool *s_pCommandBufferPool = nullptr;
    //-----------------------------------------------------

    void CommandBufferPool::Init()
    {
        s_pCommandBufferPool = new CommandBufferPool();
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
                    s_pCommandBufferPool->_cmd_buffers.emplace_back(true, cmd);
                }
                return;
            }
                AL_ASSERT_MSG(false, "Unsupport render api!");
        }
    }
    void CommandBufferPool::Shutdown()
    {
        DESTORY_PTR(s_pCommandBufferPool);
    }

    std::shared_ptr<CommandBuffer> CommandBufferPool::Get(const String &name, ECommandBufferType type)
    {
        for (auto &cmd: s_pCommandBufferPool->_cmd_buffers)
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
        std::unique_lock<std::mutex> lock(s_pCommandBufferPool->_mutex);
        if (Renderer::GetAPI() == RendererAPI::ERenderAPI::kDirectX12)
        {
            u32 newId = (u32) s_pCommandBufferPool->_cmd_buffers.size();
            auto cmd = std::make_shared<D3DCommandBuffer>(newId, type);
            s_pCommandBufferPool->_cmd_buffers.emplace_back(false, cmd);
            ++s_pCommandBufferPool->_cur_pool_size;
            if (s_pCommandBufferPool->_cur_pool_size > 100)
                LOG_WARNING("Expand commandbuffer pool to {} with name {} at frame {}", s_pCommandBufferPool->_cur_pool_size, name, Application::s_frame_count);
            cmd->SetName(name);
            cmd->Clear();
            return cmd;
        }
        else
            AL_ASSERT_MSG(false, "None render api used!");
    }

    void CommandBufferPool::Release(std::shared_ptr<CommandBuffer> &cmd)
    {
        std::unique_lock<std::mutex> lock(s_pCommandBufferPool->_mutex);
        std::get<0>(s_pCommandBufferPool->_cmd_buffers[cmd->GetID()]) = true;
        //cmd->Clear();
    }

    void CommandBufferPool::ReleaseAll()
    {
        std::unique_lock<std::mutex> lock(s_pCommandBufferPool->_mutex);
        for (auto &it: s_pCommandBufferPool->_cmd_buffers)
        {
            auto &[available, cmd] = it;
            available = true;
            cmd->Clear();
        }
    }
}// namespace Ailu
