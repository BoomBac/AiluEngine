#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__

#include <wrl/client.h>
#include <d3dx12.h>
#include "Render/CommandBuffer.h"

namespace Ailu
{
    using Microsoft::WRL::ComPtr;
    class D3DCommandBuffer : public CommandBuffer
    {
    public:
        D3DCommandBuffer();
        D3DCommandBuffer(u32 id);
        void Clear() final;
        //close before execute
        void Close() final;
        virtual u32 GetID() final { return _id; };
        const String& GetName() const final { return _name; };
        void SetName(const String& name) final{ 
            _p_cmd->SetName(ToWChar(name));
            _name = name;
        };

        void SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot = 255) final;

        void ClearRenderTarget(RenderTexture* color, RenderTexture* depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Vector<RenderTexture*>& colors, RenderTexture* depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(RenderTexture* color, Vector4f clear_color, u16 index = 0u) final;
        void ClearRenderTarget(RenderTexture* depth, float depth_value = 1.0f, u8 stencil_value = 0u) final;
        void ClearRenderTarget(RenderTexture* depth, u16 index, float depth_value = 1.0f) final;
        void SetRenderTarget(RenderTexture* color, RenderTexture* depth) final;
        void SetRenderTargets(Vector<RenderTexture*>& colors, RenderTexture* depth) final;
        void SetRenderTarget(RenderTexture* color, u16 index = 0u) final;
        void SetRenderTarget(RenderTexture* color, RenderTexture* depth, u16 color_index, u16 depth_index) final;
        void SetRenderTarget(RTHandle color, RTHandle depth) final;
        void SetRenderTarget(RTHandle color, u16 index = 0u) final;

        void ClearRenderTarget(RTHandle color, RTHandle depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Vector<RTHandle>& colors, RTHandle depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(RTHandle color, Vector4f clear_color, u16 index = 0u) final;
        void ClearRenderTarget(RTHandle depth, float depth_value = 1.0f, u8 stencil_value = 0u) final;
        void SetRenderTargets(Vector<RTHandle>& colors, RTHandle depth) final;

        void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, u32 instance_count) final;
        void DrawIndexedInstanced(u32 index_count, u32 instance_count) final;
        void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, u32 instance_count) final;
        void DrawInstanced(u32 vert_count, u32 instance_count) final;

        void SetViewport(const Rect& viewport) final;
        void SetScissorRect(const Rect& rect) final;
        void SetViewports(const std::initializer_list<Rect>& viewports) final;
        void SetScissorRects(const std::initializer_list<Rect>& rects) final;
        void SetViewports(const Vector<Rect>& viewports) final;
        void SetScissorRects(const Vector<Rect>& rects) final;

        u16 DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u32 instance_count = 1u) final;
        u16 DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u16 submesh_index, u32 instance_count = 1u) final;
        u16 DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u16 submesh_index, u16 pass_index, u32 instance_count) final;
        u16 DrawRenderer(Mesh* mesh, Material* material, u32 instance_count = 1u) final;
        u16 DrawRenderer(Mesh* mesh, Material* material, u32 instance_count, u16 pass_index) final;
        void ResolveToBackBuffer(RenderTexture* color) final;
        void ResolveToBackBuffer(RenderTexture* color, RenderTexture* destination) final;
        void ResolveToBackBuffer(RTHandle color) final;
        void ResolveToBackBuffer(RTHandle color, RTHandle destination) final;
        void Dispatch(ComputeShader* cs, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) final;

        ID3D12GraphicsCommandList* GetCmdList() { return _p_cmd.Get(); }
        u16 GetDescriptorHeapId() const { return _cur_cbv_heap_id; }
        void SetDescriptorHeapId(u16 id) { _cur_cbv_heap_id = id; };
    private:
        String _name;
        u32 _id = 0u;
        bool _b_cmd_closed;
        u16 _cur_cbv_heap_id = 65535u;
        bool _is_custom_viewport = false;
        ComPtr<ID3D12GraphicsCommandList> _p_cmd;
        ComPtr<ID3D12CommandAllocator> _p_alloc;
    };
}


#endif // !D3D_COMMAND_BUF_H__
