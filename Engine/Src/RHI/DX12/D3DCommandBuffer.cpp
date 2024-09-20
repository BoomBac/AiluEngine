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
        static bool s_is_init = false;
        if (s_is_init == false)
        {
            s_is_init = true;
            for (auto &p: s_obj_buffers)
            {
                p.reset(IConstantBuffer::Create(256));
            }
        }
        auto dev = D3DContext::Get()->GetDevice();
        ThrowIfFailed(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(_p_alloc.GetAddressOf())));
        ThrowIfFailed(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _p_alloc.Get(), nullptr, IID_PPV_ARGS(_p_cmd.GetAddressOf())));
        _b_cmd_closed = false;
        _upload_buf = MakeScope<UploadBuffer>();
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
        _upload_buf->Reset();
        s_global_buffer_offset -= _buffer_offset;
        _buffer_offset = 0;
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
        g_pGfxContext->TrackResource(color);
        g_pGfxContext->TrackResource(depth);
        _p_cmd->ClearRenderTargetView(*crt->TargetCPUHandle(this), clear_color, 0, nullptr);
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clear_depth, 0, 0, nullptr);
    }
    void D3DCommandBuffer::ClearRenderTarget(Vector<RenderTexture *> &colors, RenderTexture *depth, Vector4f clear_color, float clear_depth)
    {
        for (auto &color: colors)
        {
            if (color == nullptr)
                break;
            auto crt = static_cast<D3DRenderTexture *>(color);
            g_pGfxContext->TrackResource(color);
            _p_cmd->ClearRenderTargetView(*crt->TargetCPUHandle(this), clear_color, 0, nullptr);
        }
        auto drt = static_cast<D3DRenderTexture *>(depth);
        g_pGfxContext->TrackResource(depth);
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clear_depth, 0, 0, nullptr);
    }
    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *color, Vector4f clear_color, u16 index)
    {
        auto crt = static_cast<D3DRenderTexture *>(color);
        g_pGfxContext->TrackResource(color);
        _p_cmd->ClearRenderTargetView(*crt->TargetCPUHandle(this, index), clear_color, 0, nullptr);
    }
    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *depth, float depth_value, u8 stencil_value)
    {
        auto drt = static_cast<D3DRenderTexture *>(depth);
        g_pGfxContext->TrackResource(depth);
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth_value, stencil_value, 0, nullptr);
    }

    void D3DCommandBuffer::ClearRenderTarget(RenderTexture *depth, u16 index, float depth_value)
    {
        auto drt = static_cast<D3DRenderTexture *>(depth);
        g_pGfxContext->TrackResource(depth);
        _p_cmd->ClearDepthStencilView(*drt->TargetCPUHandle(this, index), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth_value, 0, 0, nullptr);
    }

    void D3DCommandBuffer::DrawIndexedInstanced(const std::shared_ptr<IIndexBuffer> &index_buffer, const Matrix4x4f &transform, u32 instance_count)
    {
        ++RenderingStates::s_draw_call;
        _p_cmd->DrawIndexedInstanced(index_buffer->GetCount(), instance_count, 0, 0, 0);
    }
    void D3DCommandBuffer::DrawIndexedInstanced(u32 index_count, u32 instance_count)
    {
        ++RenderingStates::s_draw_call;
        _p_cmd->DrawIndexedInstanced(index_count, instance_count, 0, 0, 0);
    }
    void D3DCommandBuffer::DrawInstanced(const std::shared_ptr<IVertexBuffer> &vertex_buf, const Matrix4x4f &transform, u32 instance_count)
    {
        ++RenderingStates::s_draw_call;
        _p_cmd->DrawInstanced(vertex_buf->GetVertexCount(), instance_count, 0, 0);
    }
    void D3DCommandBuffer::DrawInstanced(u32 vert_count, u32 instance_count)
    {
        ++RenderingStates::s_draw_call;
        _p_cmd->DrawInstanced(vert_count, instance_count, 0, 0);
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
        GraphicsPipelineStateMgr::EndConfigurePSO(this);
        if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
        {
            mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            _p_cmd->SetGraphicsRootConstantBufferView(0, alloc.GPU);
            ++RenderingStates::s_draw_call;
            _p_cmd->DrawIndexedInstanced(mesh->GetIndicesCount(submesh_index), instance_count, 0, 0, 0);
        }
        else
        {
            LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return;
        }
        return;
    }
    void D3DCommandBuffer::SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj)
    {
        g_pGfxContext->GetPipeline()->GetRenderer()->SetViewProjectionMatrix(view, proj);
    }
    void D3DCommandBuffer::SetGlobalBuffer(const String &name, void *data, u64 data_size)
    {
        auto alloc = _upload_buf->Allocate(data_size, 256);
        alloc.SetData(data, data_size);
        GraphicsPipelineStateMgr::SubmitBindResource(&alloc.GPU, EBindResDescType::kConstBufferRaw, name, PipelineResourceInfo::kPriporityCmd);
    }

    void D3DCommandBuffer::SetGlobalBuffer(const String &name, IConstantBuffer *buffer)
    {
        GraphicsPipelineStateMgr::SubmitBindResource(buffer, EBindResDescType::kConstBuffer, name, PipelineResourceInfo::kPriporityCmd);
    }


    void D3DCommandBuffer::SetGlobalTexture(const String &name, Texture *tex)
    {
        GraphicsPipelineStateMgr::SubmitBindResource(tex, EBindResDescType::kTexture2D, name, PipelineResourceInfo::kPriporityCmd);
    }
    void D3DCommandBuffer::SetGlobalTexture(const String &name, RTHandle handle)
    {
        GraphicsPipelineStateMgr::SubmitBindResource(g_pRenderTexturePool->Get(handle), EBindResDescType::kTexture2D, name, PipelineResourceInfo::kPriporityCmd);
    }
    void D3DCommandBuffer::DrawFullScreenQuad(Material *mat, u16 pass_index)
    {
        Mesh::s_p_fullscreen_triangle.lock()->GetIndexBuffer()->Bind(this);
        mat->Bind(pass_index);
        GraphicsPipelineStateMgr::EndConfigurePSO(this);
        auto d3dcmd = static_cast<D3DCommandBuffer *>(this)->GetCmdList();
        if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
        {
            DrawIndexedInstanced(3, 1);
        }
    }
    void D3DCommandBuffer::SetScissorRect(const Rect &rect)
    {
        _is_custom_viewport = true;
        auto d3d_rect = CD3DX12_RECT(rect.left, rect.top, rect.width, rect.height);
        _p_cmd->RSSetScissorRects(1, &d3d_rect);
    }

    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u32 instance_count)
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

    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u16 submesh_index, u32 instance_count)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            //LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return 1;
        }
        material->Bind();
        GraphicsPipelineStateMgr::EndConfigurePSO(this);
        if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
        {
            mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            per_obj_cbuf->Bind(this, 0);
            ++RenderingStates::s_draw_call;
            _p_cmd->DrawIndexedInstanced(mesh->GetIndicesCount(submesh_index), instance_count, 0, 0, 0);
        }
        else
        {
            LOG_WARNING("GraphicsPipelineStateMgr is not ready for current draw call! with material: {}", material != nullptr ? material->Name() : "null");
            return 2;
        }
        return 0;
    }

    u16 D3DCommandBuffer::DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u16 submesh_index, u16 pass_index, u32 instance_count)
    {
        if (mesh == nullptr || !mesh->_is_rhi_res_ready || material == nullptr || !material->IsReadyForDraw())
        {
            //LOG_WARNING("Mesh/Material is null or is not ready when draw renderer!");
            return 1;
        }
        material->Bind(pass_index);
        GraphicsPipelineStateMgr::EndConfigurePSO(this);
        if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
        {
            mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            per_obj_cbuf->Bind(this, 0);
            ++RenderingStates::s_draw_call;
            _p_cmd->DrawIndexedInstanced(mesh->GetIndicesCount(submesh_index), instance_count, 0, 0, 0);
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
        GraphicsPipelineStateMgr::EndConfigurePSO(this);
        if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
        {
            IConstantBuffer *buf = s_obj_buffers[s_global_buffer_offset + _buffer_offset].get();
            s_global_buffer_offset++;
            _buffer_offset++;
            memcpy(buf->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
            mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer(submesh_index)->Bind(this);
            buf->Bind(this, 0);
            ++RenderingStates::s_draw_call;
            _p_cmd->DrawIndexedInstanced(mesh->GetIndicesCount(submesh_index), instance_count, 0, 0, 0);
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
        GraphicsPipelineStateMgr::EndConfigurePSO(this);
        if (GraphicsPipelineStateMgr::IsReadyForCurrentDrawCall())
        {
            mesh->GetVertexBuffer()->Bind(this, material->GetShader()->PipelineInputLayout());
            mesh->GetIndexBuffer()->Bind(this);
            ++RenderingStates::s_draw_call;
            _p_cmd->DrawIndexedInstanced(mesh->GetIndexBuffer()->GetCount(), instance_count, 0, 0, 0);
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
            g_pGfxContext->TrackResource(colors[i]);
            ++rt_num;
        }
        g_pGfxContext->TrackResource(depth);
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
        g_pGfxContext->TrackResource(color);
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
            g_pGfxContext->TrackResource(color);
            g_pGfxContext->TrackResource(depth);
            GraphicsPipelineStateMgr::SetRenderTargetState(color->PixelFormat(), depth->PixelFormat(), 0);
            _p_cmd->OMSetRenderTargets(1, crt->TargetCPUHandle(this, color_index), 0, drt->TargetCPUHandle(this, depth_index));
        }
        else
        {
            if (color == nullptr)
            {
                GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat::kALGFormatUnknown, depth->PixelFormat(), 0);
                auto drt = static_cast<D3DRenderTexture *>(depth);
                g_pGfxContext->TrackResource(depth);
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
            g_pGfxContext->TrackResource(color);
            g_pGfxContext->TrackResource(depth);
            GraphicsPipelineStateMgr::SetRenderTargetState(color->PixelFormat(), depth->PixelFormat(), 0);
            _p_cmd->OMSetRenderTargets(1, crt->TargetCPUHandle(this), 0, drt->TargetCPUHandle(this));
        }
        else
        {
            if (color == nullptr)
            {
                GraphicsPipelineStateMgr::SetRenderTargetState(EALGFormat::EALGFormat::kALGFormatUnknown, depth->PixelFormat(), 0);
                auto drt = static_cast<D3DRenderTexture *>(depth);
                g_pGfxContext->TrackResource(depth);
                _p_cmd->OMSetRenderTargets(0, nullptr, 0, drt->TargetCPUHandle(this));
            }
        }
    }

    void D3DCommandBuffer::Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z)
    {
        for (auto it: cs->AllBindTexture())
        {
            g_pGfxContext->TrackResource(it);
        }
        cs->ClearBindTexture();
        cs->Bind(this, kernel, thread_group_x, thread_group_y, thread_group_z);
    }
}// namespace Ailu
