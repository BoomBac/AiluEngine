//
// Created by 22292 on 2025/3/17.
//
#include "Render/GfxCommand.h"
#include "pch.h"

namespace Ailu
{
    static CommandPool* g_pCommandPool = nullptr;
    CommandPool& CommandPool::Get()
    {
        return *g_pCommandPool;
    }
    void CommandPool::Init()
    {
        if (g_pCommandPool == nullptr)
        {
            g_pCommandPool = new CommandPool();
        }
    }
    void CommandPool::Shutdown()
    {
    }
}