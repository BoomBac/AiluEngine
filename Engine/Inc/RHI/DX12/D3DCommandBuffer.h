#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__

#include "Render/CommandBuffer.h"
#include "UploadBuffer.h"
#include <d3dx12.h>
#include <wrl/client.h>

namespace Ailu
{
    using Microsoft::WRL::ComPtr;
    class D3DCommandBuffer : public CommandBuffer
    {
    public:
        D3DCommandBuffer(ECommandBufferType type = ECommandBufferType::kCommandBufTypeDirect);
        D3DCommandBuffer(u32 id, ECommandBufferType type = ECommandBufferType::kCommandBufTypeDirect);
        void Clear() final;
        //close before execute
        void Close() final;
        virtual u32 GetID() final { return _id; };
        const String &GetName() const final { return _name; };
        void SetName(const String &name) final
        {
            auto wname = ToWChar(name);
            _p_cmd->SetName(wname);
            _p_alloc->SetName(wname);
            _name = name;
        };
        ECommandBufferType GetType() const final { return _type; }

        void ClearRenderTarget(RenderTexture *color, RenderTexture *depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Vector<RenderTexture *> &colors, RenderTexture *depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(RenderTexture *color, Vector4f clear_color, u16 index = 0u) final;
        void ClearRenderTarget(RenderTexture *depth, float depth_value = 1.0f, u8 stencil_value = 0u) final;
        void ClearRenderTarget(RenderTexture *depth, u16 index, float depth_value = 1.0f) final;
        void SetRenderTarget(RenderTexture *color, RenderTexture *depth) final;
        void SetRenderTargets(Vector<RenderTexture *> &colors, RenderTexture *depth) final;
        void SetRenderTarget(RenderTexture *color, u16 index = 0u) final;
        void SetRenderTarget(RenderTexture *color, RenderTexture *depth, u16 color_index, u16 depth_index) final;
        void SetRenderTarget(RTHandle color, RTHandle depth) final;
        void SetRenderTarget(RTHandle color, u16 index = 0u) final;

        void ClearRenderTarget(RTHandle color, RTHandle depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Vector<RTHandle> &colors, RTHandle depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(RTHandle color, Vector4f clear_color, u16 index = 0u) final;
        void ClearRenderTarget(RTHandle depth, float depth_value = 1.0f, u8 stencil_value = 0u) final;
        void SetRenderTargets(Vector<RTHandle> &colors, RTHandle depth) final;

        RTHandle GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access);
        void ReleaseTempRT(RTHandle handle);

        void DrawIndexedInstanced(const std::shared_ptr<IIndexBuffer> &index_buffer, const Matrix4x4f &transform, u32 instance_count) final;
        void DrawIndexedInstanced(u32 index_count, u32 instance_count) final;
        void DrawInstanced(const std::shared_ptr<IVertexBuffer> &vertex_buf, const Matrix4x4f &transform, u32 instance_count) final;
        void DrawInstanced(u32 vert_count, u32 instance_count) final;

        void SetViewport(const Rect &viewport) final;
        void SetScissorRect(const Rect &rect) final;
        void SetViewports(const std::initializer_list<Rect> &viewports) final;
        void SetScissorRects(const std::initializer_list<Rect> &rects) final;
        void SetViewports(const Vector<Rect> &viewports) final;
        void SetScissorRects(const Vector<Rect> &rects) final;

        void Blit(RTHandle src, RTHandle dst, Material *mat = nullptr, u16 pass_index = 0u) final;
        void Blit(RenderTexture *src, RenderTexture *dst, Material *mat = nullptr, u16 pass_index = 0u) final;
        void Blit(RenderTexture *src, RenderTexture *dst, u16 src_view_index, u16 dst_view_index, Material *mat = nullptr, u16 pass_index = 0u) final;
        void Blit(RTHandle src, RenderTexture *dst, Material *mat = nullptr, u16 pass_index = 0u) final;
        void Blit(RenderTexture *src, RTHandle dst, Material *mat = nullptr, u16 pass_index = 0u) final;

        void DrawMesh(Mesh *mesh, Material *material, const Matrix4x4f &world_matrix, u16 submesh_index = 0u, u32 instance_count = 1u) final;

        void SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj);
        void SetGlobalBuffer(const String &name, void *data, u64 data_size) final;
        void SetGlobalBuffer(const String &name, IConstantBuffer *buffer) final;
        void SetGlobalTexture(const String &name, Texture *tex) final;
        void SetGlobalTexture(const String &name, RTHandle handle) final;

        void DrawFullScreenQuad(Material *mat, u16 pass_index = 0u);
        u16 DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u32 instance_count = 1u) final;
        u16 DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u16 submesh_index, u32 instance_count = 1u) final;
        u16 DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u16 submesh_index, u16 pass_index, u32 instance_count) final;
        u16 DrawRenderer(Mesh *mesh, Material *material, const Matrix4x4f &world_mat, u16 submesh_index, u16 pass_index, u32 instance_count) final;
        u16 DrawRenderer(Mesh *mesh, Material *material, const CBufferPerObjectData &per_obj_data, u16 submesh_index, u16 pass_index, u32 instance_count) final;
        u16 DrawRenderer(Mesh *mesh, Material *material, u32 instance_count = 1u) final;
        u16 DrawRenderer(Mesh *mesh, Material *material, u32 instance_count, u16 pass_index) final;

        void Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) final;

        ID3D12GraphicsCommandList *GetCmdList() { return _p_cmd.Get(); }
        u16 GetDescriptorHeapId() const { return _cur_cbv_heap_id; }
        void SetDescriptorHeapId(u16 id) { _cur_cbv_heap_id = id; };

    private:
        inline static Array<Scope<IConstantBuffer>, 20> s_obj_buffers{};
        inline static u16 s_global_buffer_offset = 0u;
        u16 _buffer_offset = 0u;
        String _name;
        u32 _id = 0u;
        bool _b_cmd_closed;
        u16 _cur_cbv_heap_id = 65535u;
        bool _is_custom_viewport = false;
        ComPtr<ID3D12GraphicsCommandList> _p_cmd;
        ComPtr<ID3D12CommandAllocator> _p_alloc;
        Map<String, Texture *> _texture_used;
        Scope<UploadBuffer> _upload_buf;
        ECommandBufferType _type;
    };
}// namespace Ailu


#endif// !D3D_COMMAND_BUF_H__
