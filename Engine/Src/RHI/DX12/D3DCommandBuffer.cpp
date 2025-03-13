#include "RHI/DX12/D3DCommandBuffer.h"
#include "Ext/pix/Include/WinPixEventRuntime/pix3.h"
#include "Framework/Common/ResourceMgr.h"
#include "RHI/DX12/D3DContext.h"
#include "RHI/DX12/D3DTexture.h"
#include "RHI/DX12/dxhelper.h"
#include "Render/Gizmo.h"
#include "Render/GraphicsPipelineStateObject.h"
#include "Render/RenderQueue.h"
#include "Render/RenderingData.h"
#include "pch.h"

namespace Ailu
{
    D3DCommandBuffer::D3DCommandBuffer(ECommandBufferType type) : _type(type)
    {
        auto dev = D3DContext::Get()->GetDevice();
        ThrowIfFailed(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_p_alloc.GetAddressOf())));
        ThrowIfFailed(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _p_alloc.Get(), nullptr, IID_PPV_ARGS(_p_cmd.GetAddressOf())));
        _b_cmd_closed = false;
        _upload_buf = MakeScope<UploadBuffer>(std::format("CmdUploadBuffer_{}", _id));
    }
    D3DCommandBuffer::D3DCommandBuffer(u32 id, ECommandBufferType type) : D3DCommandBuffer(type)
    {
        _id = id;
    }

    void D3DCommandBuffer::Clear()
    {
        if (_b_cmd_closed)
        {
            ThrowIfFailed(_p_alloc->Reset());
            ThrowIfFailed(_p_cmd->Reset(_p_alloc.Get(), nullptr));
            _b_cmd_closed = false;
        }
        _cur_cbv_heap_id = 65535;
        _is_custom_viewport = false;
        _allocations.clear();
        _upload_buf->Reset();
    }
    void D3DCommandBuffer::Close()
    {
        if (!_b_cmd_closed)
        {
            ThrowIfFailed(_p_cmd->Close());
            _b_cmd_closed = true;
        }
    }

    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *color, RenderTexture *depth, Vector4f clear_color, float clear_depth)
    {
        auto crt = static_cast<D3DRenderTexture *>(color);
        auto drt = static_cast<D3DRenderTexture *>(depth);
        color->GetState().Track();
        depth->GetState().Track();
        _p_cmd->ClearRenderTargetView(*crt->TargetCPUHandle(this), clear_color, 0, nullptr);
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clear_depth, 0u, 0, nullptr);
    }
    void D3DCommandBuffer::ClearRenderTarget(Vector<RenderTexture *> &colors, RenderTexture *depth, Vector4f clear_color, float clear_depth)
    {
        for (auto &color: colors)
        {
            if (color == nullptr)
                break;
            auto crt = static_cast<D3DRenderTexture *>(color);
            color->GetState().Track();
            _p_cmd->ClearRenderTargetView(*crt->TargetCPUHandle(this), clear_color, 0, nullptr);
        }
        auto drt = static_cast<D3DRenderTexture *>(depth);
        depth->GetState().Track();
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clear_depth, 0u, 0, nullptr);
    }
    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *color, Vector4f clear_color, u16 index)
    {
        auto crt = static_cast<D3DRenderTexture *>(color);
        color->GetState().Track();
        _p_cmd->ClearRenderTargetView(*crt->TargetCPUHandle(this, index), clear_color, 0, nullptr);
    }
    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *depth, float depth_value, u8 stencil_value)
    {
        auto drt = static_cast<D3DRenderTexture *>(depth);
        depth->GetState().Track();
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth_value, stencil_value, 0, nullptr);
    }

    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *depth, u16 index, float depth_value)
    {
        auto drt = static_cast<D3DRenderTexture *>(depth);
        depth->GetState().Track();
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this, index), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth_value, 0u, 0, nullptr);
    }

    void D3DCommandBuffer::DrawIndexed(IVertexBuffer *vb, IIndexBuffer *ib, ConstantBuffer *cb_per_draw, Material *mat, u16 pass_index)
    {
        mat->Bind();
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            vb->Bind(this, mat->GetShader()->PipelineInputLayout());
            ib->Bind(this);
            cb_per_draw->Bind(this, 0);
            ++RenderingStates::s_draw_call;
            RenderingStates::s_vertex_num += vb->GetVertexCount();
            RenderingStates::s_triangle_num += ib->GetCount() / 3;
            _p_cmd->DrawIndexedInstanced(ib->GetCount(), 1, 0, 0, 0);
        }
    }

    void D3DCommandBuffer::SetViewports(const std::initializer_list<Rect> &viewports)
    {
        Vector<Rect> rects{viewports};
        SetViewports(rects);
    }
    void D3DCommandBuffer::SetViewport(const Rect &viewport)
    {
        _is_custom_viewport = true;
        auto rect = CD3DX12_VIEWPORT(viewport.left, viewport.top, viewport.width, viewport.height);
        _p_cmd->RSSetViewports(1, &rect);
    }
    void D3DCommandBuffer::SetScissorRects(const std::initializer_list<Rect> &rects)
    {
        Vector<Rect> rs{rects};
        SetScissorRects(rs);
    }
    void D3DCommandBuffer::SetViewports(const Vector<Rect> &viewports)
    {
        _is_custom_viewport = true;
        static CD3DX12_VIEWPORT d3d_viewports[8];
        u8 nums = 0u;
        for (auto it = viewports.begin(); it != viewports.end(); it++)
        {
            d3d_viewports[nums++] = CD3DX12_VIEWPORT(it->left, it->top, it->width, it->height);
        }
        _p_cmd->RSSetViewports(nums, d3d_viewports);
    }
    void D3DCommandBuffer::SetScissorRects(const Vector<Rect> &rects)
    {
        _is_custom_viewport = true;
        static CD3DX12_RECT d3d_rects[8];
        u8 nums = 0u;
        for (auto it = rects.begin(); it != rects.end(); it++)
        {
            d3d_rects[nums++] = CD3DX12_RECT(it->left, it->top, it->width, it->height);
        }
        _p_cmd->RSSetScissorRects(nums, d3d_rects);
    }
    void D3DCommandBuffer::Blit(RTHandle src, RTHandle dst, Material *mat, u16 pass_index)
    {
        RenderTexture *src_tex = g_pRenderTexturePool->Get(src);
        RenderTexture *dst_tex = g_pRenderTexturePool->Get(dst);
        Blit(src_tex, dst_tex, mat, pass_index);
    }
    void D3DCommandBuffer::Blit(RenderTexture *src, RenderTexture *dst, Material *mat, u16 pass_index)
    {
        static const auto blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        mat = mat ? mat : blit_mat;
        SetRenderTarget(dst);
        ClearRenderTarget(dst, Colors::kBlack);
        mat->SetTexture("_SourceTex", src);
        DrawFullScreenQuad(mat, pass_index);
    }

    void D3DCommandBuffer::Blit(RenderTexture *src, RenderTexture *dst, u16 src_view_index, u16 dst_view_index, Material *mat, u16 pass_index)
    {
        static const auto blit_mat = g_pResourceMgr->Get<Material>(L"Runtime/Material/Blit");
        mat = mat ? mat : blit_mat;
        SetRenderTarget(dst, dst_view_index);
        ClearRenderTarget(dst, Colors::kBlack, dst_view_index);
        mat->SetTexture("_SourceTex", src);
        DrawFullScreenQuad(mat, pass_index);
    }

    void D3DCommandBuffer::Blit(RTHandle src, RenderTexture *dst, Material *mat, u16 pass_index)
    {
        RenderTexture *src_tex = g_pRenderTexturePool->Get(src);
        Blit(src_tex, dst, mat, pass_index);
    }
    void D3DCommandBuffer::Blit(RenderTexture *src, RTHandle dst, Material *mat, u16 pass_index)
    {
        RenderTexture *dst_tex = g_pRenderTexturePool->Get(dst);
        Blit(src, dst_tex, mat, pass_index);
    }
    void D3DCommandBuffer::DrawMesh(Mesh *mesh, Material *material, const Matrix4x4f &world_matrix, u16 submesh_index, u32 instance_count)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            //LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return;
        }
        material->Bind();
        auto alloc = _upload_buf->Allocate(RenderConstants::kPerObjectDataSize, 256);
        CBufferPerObjectData per_obj_data;
        per_obj_data._MatrixWorld = world_matrix;
        memcpy(alloc.CPU, &per_obj_data, RenderConstants::kPerObjectDataSize);
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            auto res = PipelineResource(_upload_buf.get(),EBindResDescType::kConstBufferRaw,0,PipelineResource::kPriorityCmd);
            res._addi_info._gpu_handle = alloc.GPU;
            pso->SetPipelineResource(res);
            mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            //_p_cmd->SetGraphicsRootConstantBufferView(0, alloc.GPU);
            ++RenderingStates::s_draw_call;
            RenderingStates::s_vertex_num += mesh->GetVertexBuffer()->GetVertexCount();
            u64 idx_num = mesh->GetIndicesCount(submesh_index);
            RenderingStates::s_triangle_num += idx_num / 3;
            pso->Bind(this);
            _p_cmd->DrawIndexedInstanced(idx_num, instance_count, 0, 0, 0);
        }
        else
        {
            LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return;
        }
    }
    void D3DCommandBuffer::SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj)
    {
        g_pGfxContext->GetPipeline()->GetRenderer()->SetViewProjectionMatrix(view, proj);
    }
    void D3DCommandBuffer::SetGlobalBuffer(const String &name, void *data, u64 data_size)
    {
        _allocations.emplace_back(_upload_buf->Allocate(data_size, 256));
        _allocations.back().SetData(data, data_size);
        PipelineResource resource = PipelineResource(_upload_buf.get(),EBindResDescType::kConstBufferRaw,name, PipelineResource::kPriorityCmd);
        resource._addi_info._gpu_handle = _allocations.back().GPU;
        GraphicsPipelineStateMgr::SubmitBindResource(resource);
    }

    void D3DCommandBuffer::SetGlobalBuffer(const String &name, ConstantBuffer *buffer)
    {
        PipelineResource resource = PipelineResource(buffer,EBindResDescType::kConstBuffer,name, PipelineResource::kPriorityCmd);
        GraphicsPipelineStateMgr::SubmitBindResource(resource);
    }
    void D3DCommandBuffer::SetGlobalBuffer(const String &name, GPUBuffer *buffer)
    {
        GraphicsPipelineStateMgr::SubmitBindResource(PipelineResource(buffer, buffer->IsRandomAccess()? EBindResDescType::kRWBuffer : EBindResDescType::kBuffer, name, PipelineResource::kPriorityCmd));
    }

    void D3DCommandBuffer::SetGlobalTexture(const String &name, Texture *tex)
    {
        GraphicsPipelineStateMgr::SubmitBindResource(PipelineResource(tex, EBindResDescType::kTexture2D, name, PipelineResource::kPriorityCmd));
    }
    void D3DCommandBuffer::SetGlobalTexture(const String &name, RTHandle handle)
    {
        GraphicsPipelineStateMgr::SubmitBindResource(PipelineResource(g_pRenderTexturePool->Get(handle), EBindResDescType::kTexture2D, name, PipelineResource::kPriorityCmd));
    }
    void D3DCommandBuffer::DrawFullScreenQuad(Material *mat, u16 pass_index)
    {
        Mesh::s_p_fullscreen_triangle.lock()->GetIndexBuffer()->Bind(this);
        mat->Bind(pass_index);
        auto d3dcmd = static_cast<D3DCommandBuffer *>(this)->GetCmdList();
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            ++RenderingStates::s_draw_call;
            ++RenderingStates::s_triangle_num;
            RenderingStates::s_vertex_num += 3;
            pso->Bind(this);
            _p_cmd->DrawIndexedInstanced(3,1,0,0,0);
        }
    }
    void D3DCommandBuffer::SetScissorRect(const Rect &rect)
    {
        _is_custom_viewport = true;
        auto d3d_rect = CD3DX12_RECT(rect.left, rect.top, rect.width, rect.height);
        _p_cmd->RSSetScissorRects(1, &d3d_rect);
    }

    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cbuf, u32 instance_count)
    {
        if (mesh)
        {
            for (int i = 0; i < mesh->SubmeshCount(); ++i)
            {
                DrawRenderer(mesh, material, per_obj_cbuf, i, instance_count);
            }
        }
        return 0;
    }

    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cbuf, u16 submesh_index, u32 instance_count)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            //LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return 1;
        }
        material->Bind();
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            IVertexBuffer* vb = mesh->GetVertexBuffer().get();
            vb->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            //per_obj_cbuf->Bind(this, 0);
            pso->SetPipelineResource(PipelineResource(per_obj_cbuf,EBindResDescType::kConstBuffer,0,PipelineResource::kPriorityCmd));
            ++RenderingStates::s_draw_call;
            RenderingStates::s_vertex_num += vb->GetVertexCount();
            u64 idx_count = mesh->GetIndicesCount(submesh_index);
            RenderingStates::s_triangle_num += idx_count / 3;
            pso->Bind(this);
            _p_cmd->DrawIndexedInstanced(idx_count, instance_count, 0, 0, 0);
        }
        else
        {
            LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return 2;
        }
        return 0;
    }

    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cbuf, u16 submesh_index, u16 pass_index, u32 instance_count)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return 1;
        }
        material->Bind(pass_index);
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            IVertexBuffer* vb = mesh->GetVertexBuffer().get();
            vb->Bind(this, material->GetShader()->PipelineInputLayout(pass_index));
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            pso->SetPipelineResource(PipelineResource(per_obj_cbuf,EBindResDescType::kConstBuffer,0,PipelineResource::kPriorityCmd));
            //per_obj_cbuf->Bind(this, 0);
            ++RenderingStates::s_draw_call;
            RenderingStates::s_vertex_num += vb->GetVertexCount();
            u64 idx_count = mesh->GetIndicesCount(submesh_index);
            RenderingStates::s_triangle_num += idx_count / 3;
            pso->Bind(this);
            _p_cmd->DrawIndexedInstanced(idx_count, instance_count, 0, 0, 0);
        }
        else
        {
            LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return 2;
        }
        return 0;
    }


    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, const Matrix4x4f &world_mat, u16 submesh_index, u16 pass_index, u32 instance_count)
    {
        CBufferPerObjectData per_obj_data;
        per_obj_data._MatrixWorld = world_mat;
        return DrawRenderer(mesh, material, per_obj_data, submesh_index, pass_index, instance_count);
    }

    u16 Ailu::D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, const CBufferPerObjectData &per_obj_data, u16 submesh_index, u16 pass_index, u32 instance_count)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            //LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return 1;
        }
        material->Bind(pass_index);
        SetGlobalBuffer(RenderConstants::kCBufNamePerObject, (void *) (&per_obj_data), RenderConstants::kPerObjectDataSize);
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            IVertexBuffer* vb = mesh->GetVertexBuffer().get();
            vb->Bind(this, material->GetShader()->PipelineInputLayout(pass_index));
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            ++RenderingStates::s_draw_call;
            RenderingStates::s_vertex_num += vb->GetVertexCount();
            u64 idx_count = mesh->GetIndicesCount(submesh_index);
            RenderingStates::s_triangle_num += idx_count / 3;
            pso->Bind(this);
            _p_cmd->DrawIndexedInstanced(idx_count, instance_count, 0, 0, 0);
        }
        else
        {
            LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return 2;
        }
        return 0;
    }
    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, u32 instance_count)
    {
        return DrawRenderer(mesh, material, instance_count, 0);
    }
    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, u32 instance_count, u16 pass_index)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            //LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return 1;
        }
        material->Bind(pass_index);
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            IVertexBuffer* vb = mesh->GetVertexBuffer().get();
            vb->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer()->Bind(this);
            ++RenderingStates::s_draw_call;
            RenderingStates::s_vertex_num += vb->GetVertexCount();
            u64 idx_count = mesh->GetIndicesCount();
            RenderingStates::s_triangle_num += idx_count / 3;
            pso->Bind(this);
            _p_cmd->DrawIndexedInstanced(idx_count, instance_count, 0, 0, 0);
        }
        else
        {
            //LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return 2;
        }
        return 0;
    }
    void D3DCommandBuffer::SetRenderTargets(Vector<RenderTexture *> &colors, RenderTexture *depth)
    {
        if (!_is_custom_viewport)
        {
            Vector<Rect> rects(colors.size());
            for (int i = 0; i < colors.size(); i++)
                rects[i] = Rect(0, 0, colors[i]->Width(), colors[i]->Height());
            SetViewports(rects);
            SetScissorRects(rects);
        }
        _is_custom_viewport = false;
        GraphicsPipelineStateMgr::ResetRenderTargetState();
        u16 rt_num = 0;
        static D3D12_CPU_DESCRIPTOR_HANDLE handles[8];
        auto drt = static_cast<D3DRenderTexture *>(depth);
        for (int i = 0; i < colors.size(); i++)
        {
            if (colors[i] == nullptr)
                break;
            auto crt = static_cast<D3DRenderTexture *>(colors[i]);
            GraphicsPipelineStateMgr::SetRenderTargetState(colors[i]->PixelFormat(), depth->PixelFormat(), i);
            handles[i] = *crt->TargetCPUHandle(this);
            colors[i]->GetState().Track();
            ++rt_num;
        }
        depth->GetState().Track();
        _p_cmd->OMSetRenderTargets(rt_num, handles, false, drt->TargetCPUHandle());
    }

    void D3DCommandBuffer::SetRenderTarget(RenderTexture *color, u16 index)
    {
        if (!_is_custom_viewport)
        {
            Rect r(0, 0, color->Width(), color->Height());
            SetViewport(r);
            SetScissorRect(r);
        }
        _is_custom_viewport = false;
        auto crt = static_cast<D3DRenderTexture *>(color);
        color->GetState().Track();
        GraphicsPipelineStateMgr::ResetRenderTargetState();
        GraphicsPipelineStateMgr::SetRenderTargetState(color->PixelFormat(), EALGFormat::EALGFormat::kALGFormatUnknown, 0);
        _p_cmd->OMSetRenderTargets(1, crt->TargetCPUHandle(this, index), 0,
                                   NULL);
    }

    void D3DCommandBuffer::SetRenderTarget(RenderTexture *color, RenderTexture *depth, u16 color_index, u16 depth_index)
    {
        if (!_is_custom_viewport)
        {
            auto rt = color == nullptr ? depth : color;
            Rect r(0, 0, rt->Width(), rt->Height());
            SetViewport(r);
            SetScissorRect(r);
            _is_custom_viewport = false;
        }
        GraphicsPipelineStateMgr::ResetRenderTargetState();
        if (color && depth)
        {
            auto crt = static_cast<D3DRenderTexture *>(color);
            auto drt = static_cast<D3DRenderTexture *>(depth);
            color->GetState().Track();
            depth->GetState().Track();
            GraphicsPipelineStateMgr::SetRenderTargetState(color->PixelFormat(), depth->PixelFormat(), 0);
            _p_cmd->OMSetRenderTargets(1, crt->TargetCPUHandle(this, color_index), 0, drt->TargetCPUHandle(this, depth_index));
        }
        else
        {
            if (color == nullptr)
            {
                GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat::kALGFormatUnknown, depth->PixelFormat(), 0);
                auto drt = static_cast<D3DRenderTexture *>(depth);
                depth->GetState().Track();
                _p_cmd->OMSetRenderTargets(0, nullptr, 0, drt->TargetCPUHandle(this, depth_index));
            }
        }
    }

    void D3DCommandBuffer::ClearRenderTarget(RTHandle color, RTHandle depth, Vector4f clear_color, float clear_depth)
    {
        ClearRenderTarget(g_pRenderTexturePool->Get(color), g_pRenderTexturePool->Get(depth), clear_color, clear_depth);
    }

    void D3DCommandBuffer::ClearRenderTarget(Vector<RTHandle> &colors, RTHandle depth, Vector4f clear_color, float clear_depth)
    {
        static Vector<RenderTexture *> rts(10);
        rts.clear();
        for (int i = 0; i < colors.size(); i++)
        {
            rts.emplace_back(g_pRenderTexturePool->Get(colors[i]));
        }
        ClearRenderTarget(rts, g_pRenderTexturePool->Get(depth), clear_color, clear_depth);
    }

    void D3DCommandBuffer::ClearRenderTarget(RTHandle color, Vector4f clear_color, u16 index)
    {
        ClearRenderTarget(g_pRenderTexturePool->Get(color), clear_color, index);
    }

    void D3DCommandBuffer::ClearRenderTarget(RTHandle depth, float depth_value, u8 stencil_value)
    {
        ClearRenderTarget(g_pRenderTexturePool->Get(depth), depth_value, stencil_value);
    }

    void D3DCommandBuffer::SetRenderTargets(Vector<RTHandle> &colors, RTHandle depth)
    {
        static Vector<RenderTexture *> rts(10);
        rts.clear();
        for (int i = 0; i < colors.size(); i++)
        {
            rts.emplace_back(g_pRenderTexturePool->Get(colors[i]));
        }
        SetRenderTargets(rts, g_pRenderTexturePool->Get(depth));
    }


    RTHandle D3DCommandBuffer::GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access)
    {
        return RenderTexture::GetTempRT(width, height, name, format, mipmap_chain, linear, random_access);
    }
    void D3DCommandBuffer::ReleaseTempRT(RTHandle handle)
    {
        RenderTexture::ReleaseTempRT(handle);
    }
    void D3DCommandBuffer::SetRenderTarget(RTHandle color, RTHandle depth)
    {
        SetRenderTarget(g_pRenderTexturePool->Get(color), g_pRenderTexturePool->Get(depth));
    }

    void D3DCommandBuffer::SetRenderTarget(RTHandle color, u16 index)
    {
        SetRenderTarget(g_pRenderTexturePool->Get(color), index);
    }

    void D3DCommandBuffer::SetRenderTarget(RenderTexture *color, RenderTexture *depth)
    {
        if (!_is_custom_viewport)
        {
            auto rt = color == nullptr ? depth : color;
            Rect r(0, 0, rt->Width(), rt->Height());
            SetViewport(r);
            SetScissorRect(r);
        }
        _is_custom_viewport = false;
        GraphicsPipelineStateMgr::ResetRenderTargetState();
        if (color && depth)
        {
            auto crt = static_cast<D3DRenderTexture *>(color);
            auto drt = static_cast<D3DRenderTexture *>(depth);
            color->GetState().Track();
            depth->GetState().Track();
            GraphicsPipelineStateMgr::SetRenderTargetState(color->PixelFormat(), depth->PixelFormat(), 0);
            _p_cmd->OMSetRenderTargets(1, crt->TargetCPUHandle(this), 0, drt->TargetCPUHandle(this));
        }
        else
        {
            if (color == nullptr)
            {
                GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat::kALGFormatUnknown, depth->PixelFormat(), 0);
                auto drt = static_cast<D3DRenderTexture *>(depth);
                depth->GetState().Track();
                _p_cmd->OMSetRenderTargets(0, nullptr, 0, drt->TargetCPUHandle(this));
            }
        }
    }
    void D3DCommandBuffer::Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y)
    {
        Dispatch(cs,kernel,thread_group_x,thread_group_y,1);
    }
    void D3DCommandBuffer::Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z)
    {
        RenderingStates::s_dispatch_call++;
        for (auto it: cs->AllBindTexture())
        {
            it->GetState().Track();
        }
        cs->ClearBindTexture();
        thread_group_x = std::max<u16>(1u, thread_group_x);
        thread_group_y = std::max<u16>(1u, thread_group_y);
        thread_group_z = std::max<u16>(1u, thread_group_z);
        cs->Bind(this, kernel, thread_group_x, thread_group_y, thread_group_z);
        for(auto& cb : _cs_cbuf)
        {
            u16 slot = cs->NameToSlot(cb._name,cb._kernel,cs->ActiveVariant(kernel));
            if (slot != UINT16_MAX)
                _p_cmd->SetComputeRootConstantBufferView(slot,cb._ptr);
        }
        _cs_cbuf.clear();
        _p_cmd->Dispatch(thread_group_x, thread_group_y, thread_group_z);
    }
    void D3DCommandBuffer::DrawInstanced(IVertexBuffer *vb, ConstantBuffer *cb_per_draw, Material *mat, u16 pass_index, u16 instance_count)
    {
        mat->Bind(pass_index);
        if (auto pso = GraphicsPipelineStateMgr::FindMatchPSO(); pso != nullptr)
        {
            auto shader = mat->GetShader();
            vb->Bind(this, shader->PipelineInputLayout(pass_index));
            if (cb_per_draw)
                cb_per_draw->Bind(this, 0);
            ++RenderingStates::s_draw_call;
            u64 vert_num = shader->GetTopology() == ETopology::kTriangleStrip ? vb->GetVertexCount() * 4 : vb->GetVertexCount();
            RenderingStates::s_vertex_num += vert_num;
            RenderingStates::s_triangle_num += vert_num / 3;
            pso->Bind(this);
            _p_cmd->DrawInstanced(vert_num, instance_count, 0, 0);
        }
    }
    void D3DCommandBuffer::SetComputeBuffer(const String &name, u16 kernel,void *data, u64 data_size)
    {
        _allocations.emplace_back(_upload_buf->Allocate(data_size, 256));
        _allocations.back().SetData(data, data_size);
        _cs_cbuf.emplace_back(name, kernel, _allocations.back().GPU);
    }
}// namespace Ailu
