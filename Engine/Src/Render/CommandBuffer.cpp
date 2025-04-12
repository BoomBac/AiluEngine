#include "pch.h"

#include "Framework/Common/ThreadPool.h"
#include "RHI/DX12/D3DCommandBuffer.h"
#include "Render/CommandBuffer.h"
#include "Render/GraphicsContext.h"
#include "Render/Renderer.h"
#include <Framework/Common/Application.h>
#include <Framework/Common/ResourceMgr.h>
#include <Render/GraphicsPipelineStateObject.h>
#include "Framework/Common/Profiler.h"

namespace Ailu
{
    static CommandBufferPool *s_pCommandBufferPool = nullptr;
    static RHICommandBufferPool *s_pRHICommandBufferPool = nullptr;
    //-----------------------------------------------------
#pragma region CommandBufferPool
    void CommandBufferPool::Init()
    {
        s_pCommandBufferPool = new CommandBufferPool();
        for (int i = 0; i < kInitialPoolSize; i++)
        {
            auto cmd = std::make_shared<CommandBuffer>("noname");
            s_pCommandBufferPool->_cmd_buffers.emplace_back(true, cmd);
        }
    }
    void CommandBufferPool::Shutdown()
    {
        DESTORY_PTR(s_pCommandBufferPool);
    }

    Ref<CommandBuffer> CommandBufferPool::Get(const String &name)
    {
        for (auto &cmd: s_pCommandBufferPool->_cmd_buffers)
        {
            auto &[available, cmd_buf] = cmd;

            if (available)
            {
                available = false;
                cmd_buf->Clear();
                cmd_buf->Name(name);
                return cmd_buf;
            }
        }
        std::unique_lock<std::mutex> lock(s_pCommandBufferPool->_mutex);
        //u32 newId = (u32) s_pCommandBufferPool->_cmd_buffers.size();
        auto cmd = std::make_shared<CommandBuffer>(name);
        s_pCommandBufferPool->_cmd_buffers.emplace_back(false, cmd);
        ++s_pCommandBufferPool->_cur_pool_size;
        if (s_pCommandBufferPool->_cur_pool_size > 100)
            LOG_WARNING("Expand command buffer pool to {} with name {} at frame {}", s_pCommandBufferPool->_cur_pool_size, name, Application::Application::Get().GetFrameCount());
        cmd->Name(name);
        cmd->Clear();
        return cmd;
    }

    void CommandBufferPool::Release(Ref<CommandBuffer> &cmd)
    {
        std::unique_lock<std::mutex> lock(s_pCommandBufferPool->_mutex);
        for (auto &it: s_pCommandBufferPool->_cmd_buffers)
        {
            auto &[available, cmd_buf] = it;
            if (cmd_buf == cmd)
            {
                available = true;
                return;
            }
        }
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
#pragma endregion

#pragma region CommandBuffer
    CommandBuffer::CommandBuffer(String name) :_commands()
    {
        _name = std::move(name);
    }
    void CommandBuffer::Clear()
    {
        if (_commands.size() > 0)
        {
            for (auto &cmd: _commands)
                CommandPool::Get().Free(cmd);
        }
        _commands.clear();
    }
    void CommandBuffer::ClearRenderTarget(Color color, f32 depth, u8 stencil)
    {
        auto cmd = CommandPool::Get().Alloc<CommandClearTarget>();
        cmd->_depth = depth;
        cmd->_color_target_num = 1u;
        cmd->_colors[0] = color;
        cmd->_stencil = stencil;
        cmd->_flag = EClearFlag::kAll;
        _commands.push_back(cmd);
    }
    void CommandBuffer::ClearRenderTarget(Color c)
    {
        auto cmd = CommandPool::Get().Alloc<CommandClearTarget>();
        cmd->_color_target_num = 1u;
        cmd->_colors[0] = c;
        cmd->_flag = EClearFlag::kColor;
        _commands.push_back(cmd);
    }
    void CommandBuffer::ClearRenderTarget(f32 depth, u8 stencil)
    {
        auto cmd = CommandPool::Get().Alloc<CommandClearTarget>();
        cmd->_depth = depth;
        cmd->_stencil = stencil;
        cmd->_flag = static_cast<EClearFlag>(EClearFlag::kDepth | EClearFlag::kStencil);
        _commands.push_back(cmd);
    }
    void CommandBuffer::ClearRenderTarget(Color *colors, u16 num, f32 depth, u8 stencil)
    {
        auto cmd = CommandPool::Get().Alloc<CommandClearTarget>();
        cmd->_depth = depth;
        cmd->_color_target_num = num;
        for (u16 i = 0; i < num; ++i)
            cmd->_colors[i] = colors[i];
        cmd->_stencil = stencil;
        cmd->_flag = EClearFlag::kAll;
        _commands.push_back(cmd);
    }
    void CommandBuffer::ClearRenderTarget(Color *colors, u16 num)
    {
        auto cmd = CommandPool::Get().Alloc<CommandClearTarget>();
        cmd->_color_target_num = num;
        for (u16 i = 0; i < num; ++i)
            cmd->_colors[i] = colors[i];
        cmd->_flag = EClearFlag::kColor;
        _commands.push_back(cmd);
    }

    void CommandBuffer::SetRenderTarget(RenderTexture *color, RenderTexture *depth)
    {
        auto cmd = CommandPool::Get().Alloc<CommandSetTarget>();
        cmd->_color_target_num = 1u;
        cmd->_color_target[0] = color;
        cmd->_depth_target = depth;
        cmd->_color_indices[0] = 0u;
        cmd->_depth_index = 0u;
        if (_viewports[0].width != 0u)
        {
            cmd->_viewports[0] = _viewports[0];
            _viewports[0] = Rect();
        }
        else
        {
            RenderTexture* rt = color? color : depth;
            cmd->_viewports[0] = Rect(0, 0, rt->Width(), rt->Height());
        }
        _commands.push_back(cmd);
        if (color->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(Colors::kBlack);
        if (depth->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(kZFar, 0u);
    }
    void CommandBuffer::SetRenderTarget(RenderTexture *color, u16 index)
    {
        auto cmd = CommandPool::Get().Alloc<CommandSetTarget>();
        cmd->_color_target_num = 1u;
        cmd->_color_target[0] = color;
        cmd->_color_indices[0] = index;
        if (_viewports[0].width != 0u)
        {
            cmd->_viewports[0] = _viewports[0];
            _viewports[0] = Rect();
        }
        else
            cmd->_viewports[0] = Rect(0, 0, color->Width(), color->Height());
        _commands.push_back(cmd);
        if (color->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(Colors::kBlack);
    }
    void CommandBuffer::SetRenderTarget(RenderTexture *color, RenderTexture *depth, u16 color_index, u16 depth_index)
    {
        auto cmd = CommandPool::Get().Alloc<CommandSetTarget>();
        cmd->_color_target_num = color? 1u : 0u;
        cmd->_color_target[0] = color;
        cmd->_depth_target = depth;
        cmd->_color_indices[0] = color_index;
        cmd->_depth_index = depth_index;
        if (_viewports[0].width != 0u)
        {
            cmd->_viewports[0] = _viewports[0];
            _viewports[0] = Rect();
        }
        else
        {
            RenderTexture* rt = color? color : depth;
            cmd->_viewports[0] = Rect(0, 0, rt->Width(), rt->Height());
        }
        _commands.push_back(cmd);
        if (color && color->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(Colors::kBlack);
        if (depth->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(kZFar, 0u);
    }
    void CommandBuffer::SetRenderTargets(Vector<RTHandle> &colors, RTHandle depth)
    {
        auto cmd = CommandPool::Get().Alloc<CommandSetTarget>();
        cmd->_color_target_num = (u16) colors.size();
        static Color s_clear_colors[RenderConstants::kMaxMRTNum] = {Colors::kBlack,Colors::kBlack,Colors::kBlack,Colors::kBlack,Colors::kBlack,Colors::kBlack,Colors::kBlack,Colors::kBlack};
        for (u16 i = 0; i < colors.size(); ++i)
        {
            cmd->_color_target[i] = g_pRenderTexturePool->Get(colors[i]);
            cmd->_color_indices[i] = 0u;
            if (_viewports[i].width != 0u)
            {
                cmd->_viewports[i] = _viewports[i];
                _viewports[i] = Rect();
            }
            else
                cmd->_viewports[i] = Rect(0, 0, cmd->_color_target[i]->Width(), cmd->_color_target[i]->Height());
        }
        cmd->_depth_target = g_pRenderTexturePool->Get(depth);
        cmd->_depth_index = 0u;
        _commands.push_back(cmd);
        if (cmd->_color_target[0]->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(s_clear_colors, cmd->_color_target_num);
        if (cmd->_depth_target->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(kZFar, 0u);
    }
    void CommandBuffer::SetRenderTarget(RTHandle color, RTHandle depth)
    {
        SetRenderTarget(g_pRenderTexturePool->Get(color), g_pRenderTexturePool->Get(depth));
    }
    void CommandBuffer::SetRenderTarget(RTHandle color, u16 index)
    {
        SetRenderTarget(g_pRenderTexturePool->Get(color), index);
    }
    void CommandBuffer::DrawIndexed(VertexBuffer *vb, IndexBuffer *ib, ConstantBuffer *per_obj_cb, Material *mat, u16 pass_index)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = vb;
        cmd->_ib = ib;
        cmd->_per_obj_cb = per_obj_cb;
        cmd->_mat = mat;
        cmd->_pass_index = pass_index;
        cmd->_instance_count = 1u;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.push_back(cmd);
    }
    void CommandBuffer::DrawInstanced(VertexBuffer *vb, ConstantBuffer *per_obj_cb, Material *mat, u16 pass_index, u16 instance_count)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = vb;
        cmd->_ib = nullptr;
        cmd->_per_obj_cb = per_obj_cb;
        cmd->_mat = mat;
        cmd->_pass_index = pass_index;
        cmd->_instance_count = 1u;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.push_back(cmd);
    }
    void CommandBuffer::SetViewport(const Rect &viewport)
    {
        _viewports[0] = viewport;
        if (_commands.back()->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            static_cast<CommandSetTarget *>(_commands.back())->_viewports[0] = viewport;
            _viewports[0] = Rect();
        }
    }
    void CommandBuffer::SetScissorRect(const Rect &rect)
    {
        _viewports[0] = rect;
        if (_commands.back()->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            static_cast<CommandSetTarget *>(_commands.back())->_viewports[0] = rect;
            _viewports[0] = Rect();
        }
    }
    void CommandBuffer::SetViewports(const std::initializer_list<Rect> &viewports)
    {
        u16 count = std::min((u16) viewports.size(), RenderConstants::kMaxMRTNum);
        u16 index = 0u;
        for(auto it = viewports.begin(); it != viewports.end(); ++it)
        {
            _viewports[index++] = *it;
        }
        if (_commands.back()->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            auto cmd = static_cast<CommandSetTarget *>(_commands.back());
            for (u16 i = 0; i < count; ++i)
            {
                cmd->_viewports[i] = _viewports[i];
                _viewports[i] = Rect();
            }
        }
    }
    void CommandBuffer::SetScissorRects(const std::initializer_list<Rect> &rects)
    {
        u16 count = std::min((u16) rects.size(), RenderConstants::kMaxMRTNum);
        u16 index = 0u;
        for(auto it = rects.begin(); it != rects.end(); ++it)
        {
            _viewports[index++] = *it;
        }
        if (_commands.back()->GetCmdType() == EGpuCommandType::kSetTarget)
        {
            auto cmd = static_cast<CommandSetTarget *>(_commands.back());
            for (u16 i = 0; i < count; ++i)
            {
                cmd->_viewports[i] = _viewports[i];
                _viewports[i] = Rect();
            }
        }
    }
    RTHandle CommandBuffer::GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
    {
        return RenderTexture::GetTempRT(width, height, name, format, mipmap_chain, linear, random_access);
    }
    void CommandBuffer::ReleaseTempRT(RTHandle handle)
    {
        RenderTexture::ReleaseTempRT(handle);
    }
    void CommandBuffer::Blit(RTHandle src, RTHandle dst, Material *mat, u16 pass_index)
    {
        Blit(g_pRenderTexturePool->Get(src), g_pRenderTexturePool->Get(dst), mat, pass_index);
    }
    void CommandBuffer::Blit(RenderTexture *src, RenderTexture *dst, Material *mat, u16 pass_index)
    {
        static const auto blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        mat = mat ? mat : blit_mat;
        SetRenderTarget(dst);
        if (dst->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(Colors::kBlack);
        mat->SetTexture("_SourceTex", src);
        DrawFullScreenQuad(mat, pass_index);
    }
    void CommandBuffer::Blit(RenderTexture *src, RenderTexture *dst, u16 src_view_index, u16 dst_view_index, Material *mat, u16 pass_index)
    {
        static const auto blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        mat = mat ? mat : blit_mat;
        SetRenderTarget(dst, dst_view_index);
        if (dst->_load_action == ELoadStoreAction::kClear)
            ClearRenderTarget(Colors::kBlack);
        mat->SetTexture("_SourceTex", src);
        DrawFullScreenQuad(mat, pass_index);
    }
    void CommandBuffer::Blit(RTHandle src, RenderTexture *dst, Material *mat, u16 pass_index)
    {
        Blit(g_pRenderTexturePool->Get(src), dst, mat, pass_index);
    }
    void CommandBuffer::Blit(RenderTexture *src, RTHandle dst, Material *mat, u16 pass_index)
    {
        Blit(src, g_pRenderTexturePool->Get(dst), mat, pass_index);
    }
    void CommandBuffer::DrawFullScreenQuad(Material *mat, u16 pass_index)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = Mesh::s_p_fullscreen_triangle.lock()->GetVertexBuffer().get();
        cmd->_ib = Mesh::s_p_fullscreen_triangle.lock()->GetIndexBuffer().get();
        cmd->_mat = mat;
        cmd->_instance_count = 1u;
        cmd->_sub_mesh = 0u;
        cmd->_pass_index = pass_index;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::SetGlobalBuffer(const String &name, void *data, u64 data_size)
    {
        auto cmd = CommandPool::Get().Alloc<CommandAllocConstBuffer>();
        cmd->_data = new u8[data_size];
        cmd->_size = (u32) data_size;
        memcpy(cmd->_data, data, data_size);
        memcpy(cmd->_name, name.c_str(), name.length() + 1);
        cmd->_kernel = -1;
        _commands.emplace_back(cmd);
    };
    void CommandBuffer::SetGlobalBuffer(const String &name, ConstantBuffer *buffer)
    {
        auto cmd = CommandPool::Get().Alloc<CommandCustom>();
        cmd->_func = [=]()
        {
            PipelineResource resource = PipelineResource(buffer, EBindResDescType::kConstBuffer, name, PipelineResource::kPriorityCmd);
            GraphicsPipelineStateMgr::SubmitBindResource(resource);
        };
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::SetGlobalBuffer(const String &name, GPUBuffer *buffer)
    {
        auto cmd = CommandPool::Get().Alloc<CommandCustom>();
        cmd->_func = [=]()
        {
            PipelineResource resource = PipelineResource(buffer, buffer->IsRandomAccess() ? EBindResDescType::kRWBuffer : EBindResDescType::kBuffer, name, PipelineResource::kPriorityCmd);
            GraphicsPipelineStateMgr::SubmitBindResource(resource);
        };
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::SetComputeBuffer(const String &name, u16 kernel, void *data, u64 data_size)
    {
        auto cmd = CommandPool::Get().Alloc<CommandAllocConstBuffer>();
        cmd->_data = (u8 *) data;
        cmd->_size = (u32) data_size;
        memcpy(cmd->_name, name.c_str(), name.length() + 1);
        cmd->_kernel = kernel;
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::SetGlobalTexture(const String &name, Texture *tex)
    {
        auto cmd = CommandPool::Get().Alloc<CommandCustom>();
        cmd->_func = [=]()
        {
            PipelineResource resource = PipelineResource(tex, EBindResDescType::kTexture2D, name, PipelineResource::kPriorityCmd);
            GraphicsPipelineStateMgr::SubmitBindResource(resource);
        };
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::SetGlobalTexture(const String &name, RTHandle handle)
    {
        SetGlobalTexture(name, g_pRenderTexturePool->Get(handle));
    }
    void CommandBuffer::DrawMesh(Mesh *mesh, Material *material, const Matrix4x4f &world_matrix, u16 sub_mesh, u32 instance_count)
    {
        CBufferPerObjectData per_obj_data;
        per_obj_data._MatrixWorld = world_matrix;
        SetGlobalBuffer(RenderConstants::kCBufNamePerObject, (u8 *) (&per_obj_data), RenderConstants::kPerObjectDataSize);
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = mesh->GetVertexBuffer().get();
        cmd->_ib = mesh->GetIndexBuffer().get();
        cmd->_mat = material;
        cmd->_instance_count = instance_count;
        cmd->_sub_mesh = sub_mesh;
        cmd->_pass_index = 0u;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cb, u32 instance_count)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = mesh->GetVertexBuffer().get();
        cmd->_ib = mesh->GetIndexBuffer().get();
        cmd->_per_obj_cb = per_obj_cb;
        cmd->_mat = material;
        cmd->_instance_count = instance_count;
        cmd->_sub_mesh = 0u;
        cmd->_pass_index = 0u;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cb, u16 sub_mesh, u32 instance_count)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = mesh->GetVertexBuffer().get();
        cmd->_ib = mesh->GetIndexBuffer().get();
        cmd->_per_obj_cb = per_obj_cb;
        cmd->_mat = material;
        cmd->_instance_count = instance_count;
        cmd->_sub_mesh = sub_mesh;
        cmd->_pass_index = 0u;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cb, u16 sub_mesh, u16 pass_index, u32 instance_count)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = mesh->GetVertexBuffer().get();
        cmd->_ib = mesh->GetIndexBuffer().get();
        cmd->_per_obj_cb = per_obj_cb;
        cmd->_mat = material;
        cmd->_instance_count = instance_count;
        cmd->_sub_mesh = sub_mesh;
        cmd->_pass_index = pass_index;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::DrawRenderer(Mesh *mesh, Material *material, const Matrix4x4f &world_mat, u16 sub_mesh, u16 pass_index, u32 instance_count)
    {
        CBufferPerObjectData per_obj_data;
        per_obj_data._MatrixWorld = world_mat;
        SetGlobalBuffer(RenderConstants::kCBufNamePerObject, (u8 *) (&per_obj_data), RenderConstants::kPerObjectDataSize);
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = mesh->GetVertexBuffer().get();
        cmd->_ib = mesh->GetIndexBuffer().get();
        cmd->_mat = material;
        cmd->_instance_count = instance_count;
        cmd->_sub_mesh = sub_mesh;
        cmd->_pass_index = pass_index;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::DrawRenderer(Mesh *mesh, Material *material, const CBufferPerObjectData &per_obj_data, u16 sub_mesh, u16 pass_index, u32 instance_count)
    {
        SetGlobalBuffer(RenderConstants::kCBufNamePerObject, (u8 *) (&per_obj_data), RenderConstants::kPerObjectDataSize);
        auto cmd = CommandPool::Get().Alloc<CommandDraw>();
        cmd->_vb = mesh->GetVertexBuffer().get();
        cmd->_ib = mesh->GetIndexBuffer().get();
        cmd->_mat = material;
        cmd->_instance_count = instance_count;
        cmd->_sub_mesh = sub_mesh;
        cmd->_pass_index = pass_index;
        cmd->_mat->PushState(cmd->_pass_index);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDispatch>();
        cmd->_cs = cs;
        cmd->_group_num_x = thread_group_x;
        cmd->_group_num_y = thread_group_y;
        cmd->_group_num_z = 1u;
        cmd->_kernel = kernel;
        cmd->_cs->PushState(cmd->_kernel);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z)
    {
        auto cmd = CommandPool::Get().Alloc<CommandDispatch>();
        cmd->_cs = cs;
        cmd->_group_num_x = thread_group_x;
        cmd->_group_num_y = thread_group_y;
        cmd->_group_num_z = thread_group_z;
        cmd->_kernel = kernel;
        cmd->_cs->PushState(cmd->_kernel);
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::BeginProfiler(const String &name)
    {
        auto cmd = CommandPool::Get().Alloc<CommandProfiler>();
        cmd->_name = name;
        cmd->_is_start = true;
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::EndProfiler()
    {
        auto cmd = CommandPool::Get().Alloc<CommandProfiler>();
        cmd->_is_start = false;
        _commands.emplace_back(cmd);
    }
    void CommandBuffer::SetRenderTargetLoadAction(RenderTexture *tex, ELoadStoreAction action)
    {
        tex->_load_action = action;
    }
    void CommandBuffer::SetRenderTargetLoadAction(RTHandle handle, ELoadStoreAction action)
    {
        SetRenderTargetLoadAction(g_pRenderTexturePool->Get(handle),action);
    }
    void CommandBuffer::StateTransition(GpuResource* res,EResourceState new_state,u32 sub_res)
    {
        auto cmd = CommandPool::Get().Alloc<CommandTranslateState>();
        cmd->_res = res;
        cmd->_new_state = new_state;
        cmd->_sub_res = sub_res;
        _commands.emplace_back(cmd);
    }


#pragma endregion
    void RHICommandBufferPool::Init()
    {
        s_pRHICommandBufferPool = new RHICommandBufferPool();
        for (int i = 0; i < kInitialPoolSize; i++)
        {
            Ref<RHICommandBuffer> cmd = nullptr;
            if (RendererAPI::GetAPI() == RendererAPI::ERenderAPI::kDirectX12)
                cmd = MakeRef<D3DCommandBuffer>("noname",ECommandBufferType::kCommandBufTypeDirect);
            else
            {
                AL_ASSERT(true);
            }
            s_pRHICommandBufferPool->_cmd_buffers.emplace_back(true, cmd);
        }
    }
    void RHICommandBufferPool::Shutdown()
    {
        DESTORY_PTR(s_pRHICommandBufferPool);
    }
    Ref<RHICommandBuffer> RHICommandBufferPool::Get(const String &name, ECommandBufferType type)
    {
        for (auto &cmd: s_pRHICommandBufferPool->_cmd_buffers)
        {
            auto &[available, cmd_buf] = cmd;

            if (available && cmd_buf->IsReady() && cmd_buf->GetCommandBufferType() == type)
            {
                available = false;
                cmd_buf->Clear();
                cmd_buf->Name(name);
                return cmd_buf;
            }
        }
        //std::unique_lock<std::mutex> lock(s_pRHICommandBufferPool->_mutex);
        Ref<RHICommandBuffer> cmd = nullptr;
        if (RendererAPI::GetAPI() == RendererAPI::ERenderAPI::kDirectX12)
            cmd = MakeRef<D3DCommandBuffer>(name,type);
        else
        {
            AL_ASSERT(true);
        }
        s_pRHICommandBufferPool->_cmd_buffers.emplace_back(false, cmd);
        ++s_pRHICommandBufferPool->_cur_pool_size;
        if (s_pRHICommandBufferPool->_cur_pool_size > 100)
            LOG_WARNING("Expand command buffer pool to {} with name {} at frame {}", s_pRHICommandBufferPool->_cur_pool_size, name, Application::Application::Get().GetFrameCount());
        cmd->Name(name);
        cmd->Clear();
        return cmd;
    }
    void RHICommandBufferPool::Release(Ref<RHICommandBuffer> &cmd)
    {
        //std::unique_lock<std::mutex> lock(s_pRHICommandBufferPool->_mutex);
        for (auto &it: s_pRHICommandBufferPool->_cmd_buffers)
        {
            auto &[available, cmd_buf] = it;
            if (cmd_buf == cmd)
            {
                available = true;
                return;
            }
        }
    }
}// namespace Ailu
