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
        void Close() final;
        virtual u32 GetID() final { return _id; };

        void SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot = 255) final;
        void ClearRenderTarget(Vector4f color, float depth, bool clear_color, bool clear_depth) final;
        void ClearRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Vector<Ref<RenderTexture>>& colors, Ref<RenderTexture>& depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Ref<RenderTexture>& color, Vector4f clear_color, u16 index = 0u) final;
        void ClearRenderTarget(RenderTexture* depth, float depth_value = 1.0f, u8 stencil_value = 0u) final;
        void SetRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth) final;
        void SetRenderTargets(Vector<Ref<RenderTexture>>& colors, Ref<RenderTexture>& depth) final;
        void SetRenderTarget(Ref<RenderTexture>& color, u16 index = 0u) final;
        void SetRenderTarget(RenderTexture* color, RenderTexture* depth) final;
        void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, u32 instance_count) final;
        void DrawIndexedInstanced(u32 index_count, u32 instance_count) final;
        void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, u32 instance_count) final;
        void DrawInstanced(u32 vert_count, u32 instance_count) final;
        void SetViewports(const std::initializer_list<Rect>& viewports) final;
        void SetScissorRects(const std::initializer_list<Rect>& rects) final;
        void SetViewport(const Rect& viewport) final;
        void SetScissorRect(const Rect& rect) final;
        void DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, u32 instance_count = 1u) final;
        void DrawRenderer(Mesh* mesh, Material* material, const Matrix4x4f& transform, u32 instance_count = 1u) final;
        void DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u32 instance_count = 1u) final;
        void DrawRenderer(Mesh* mesh, Material* material, u32 instance_count = 1u) final;
        void ResolveToBackBuffer(Ref<RenderTexture>& color) final;
        void Dispatch(ComputeShader* cs, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) final;

        ID3D12GraphicsCommandList* GetCmdList() { return _p_cmd.Get(); }
    private:
        u32 _id = 0u;
        bool _b_cmd_closed;
        ComPtr<ID3D12GraphicsCommandList> _p_cmd;
        ComPtr<ID3D12CommandAllocator> _p_alloc;
    };
}


#endif // !D3D_COMMAND_BUF_H__
