#pragma once
#ifndef __COMMAND_BUFFER__
#define __COMMAND_BUFFER__

#include "Framework/Math/ALMath.hpp"
#include "Material.h"
#include "Mesh.h"
#include "RendererAPI.h"
#include "Texture.h"
#include <mutex>
#include <tuple>


namespace Ailu
{
    enum class ECommandBufferType
    {
        kCommandBufTypeDirect = 0,
        kCommandBufTypeBundle = 1,
        kCommandBufTypeCompute = 2,
        kCommandBufTypeCopy = 3
        // D3D12_COMMAND_LIST_TYPE_DIRECT	= 0,
        // D3D12_COMMAND_LIST_TYPE_BUNDLE	= 1,
        // D3D12_COMMAND_LIST_TYPE_COMPUTE	= 2,
        // D3D12_COMMAND_LIST_TYPE_COPY	= 3,
        // D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE	= 4,
        // D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS	= 5,
        // D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE	= 6
    };
    class IVertexBuffer;
    class IIndexBuffer;
    class IConstantBuffer;
    class CommandBuffer
    {
    public:
        virtual ~CommandBuffer() = default;
        virtual void Clear() = 0;
        virtual void Close() = 0;
        virtual u32 GetID() = 0;
        virtual const String &GetName() const = 0;
        virtual void SetName(const String &name) = 0;
        virtual ECommandBufferType GetType() const = 0;

        virtual void ClearRenderTarget(RenderTexture *color, RenderTexture *depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(Vector<RenderTexture *> &colors, RenderTexture *depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(RenderTexture *color, Vector4f clear_color, u16 index = 0u) = 0;
        virtual void ClearRenderTarget(RenderTexture *depth, float depth_value = 1.0f, u8 stencil_value = 0u) = 0;
        virtual void ClearRenderTarget(RenderTexture *depth, u16 index, float depth_value = 1.0f) = 0;
        virtual void ClearRenderTarget(Vector<RTHandle> &colors, RTHandle depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(RTHandle color, RTHandle depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(RTHandle color, Vector4f clear_color, u16 index = 0u) = 0;
        virtual void ClearRenderTarget(RTHandle depth, float depth_value = 1.0f, u8 stencil_value = 0u) = 0;

        virtual void SetRenderTargets(Vector<RenderTexture *> &colors, RenderTexture *depth) = 0;
        virtual void SetRenderTarget(RenderTexture *color, RenderTexture *depth) = 0;
        /// <summary>
        /// 设置rendertarget，index用于cubemap或者texture array
        /// </summary>
        /// <param name="color"></param>
        /// <param name="index"></param>
        virtual void SetRenderTarget(RenderTexture *color, u16 index = 0u) = 0;
        virtual void SetRenderTarget(RenderTexture *color, RenderTexture *depth, u16 color_index, u16 depth_index) = 0;
        virtual void SetRenderTargets(Vector<RTHandle> &colors, RTHandle depth) = 0;
        virtual void SetRenderTarget(RTHandle color, RTHandle depth) = 0;
        virtual void SetRenderTarget(RTHandle color, u16 index = 0u) = 0;

        virtual void DrawIndexedInstanced(const std::shared_ptr<IIndexBuffer> &index_buffer, const Matrix4x4f &transform, u32 instance_count) = 0;
        virtual void DrawIndexedInstanced(u32 index_count, u32 instance_count) = 0;
        virtual void DrawInstanced(const std::shared_ptr<IVertexBuffer> &vertex_buf, const Matrix4x4f &transform, u32 instance_count) = 0;
        virtual void DrawInstanced(u32 vert_count, u32 instance_count) = 0;

        virtual void SetViewport(const Rect &viewport) = 0;
        virtual void SetScissorRect(const Rect &rect) = 0;
        virtual void SetViewports(const std::initializer_list<Rect> &viewports) = 0;
        virtual void SetScissorRects(const std::initializer_list<Rect> &rects) = 0;
        virtual void SetViewports(const Vector<Rect> &viewports) = 0;
        virtual void SetScissorRects(const Vector<Rect> &rects) = 0;

        virtual RTHandle GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access) = 0;
        virtual void ReleaseTempRT(RTHandle handle) = 0;

        virtual void Blit(RTHandle src, RTHandle dst, Material *mat = nullptr, u16 pass_index = 0u) = 0;
        virtual void Blit(RenderTexture *src, RenderTexture *dst, Material *mat = nullptr, u16 pass_index = 0u) = 0;
        virtual void Blit(RenderTexture *src, RenderTexture *dst, u16 src_view_index, u16 dst_view_index, Material *mat = nullptr, u16 pass_index = 0u) = 0;
        virtual void Blit(RTHandle src, RenderTexture *dst, Material *mat = nullptr, u16 pass_index = 0u) = 0;
        virtual void Blit(RenderTexture *src, RTHandle dst, Material *mat = nullptr, u16 pass_index = 0u) = 0;

        virtual void DrawFullScreenQuad(Material *mat, u16 pass_index = 0u) = 0;
        virtual void SetViewProjectionMatrix(const Matrix4x4f &view, const Matrix4x4f &proj) = 0;
        virtual void SetGlobalBuffer(const String &name, void *data, u64 data_size) = 0;
        virtual void SetGlobalBuffer(const String &name, IConstantBuffer *buffer) = 0;
        virtual void SetGlobalTexture(const String &name, Texture *tex) = 0;
        virtual void SetGlobalTexture(const String &name, RTHandle handle) = 0;

        virtual void DrawMesh(Mesh *mesh, Material *material, const Matrix4x4f &world_matrix, u16 submesh_index = 0u, u32 instance_count = 1u) = 0;

        virtual u16 DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u16 submesh_index, u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh *mesh, Material *material, IConstantBuffer *per_obj_cbuf, u16 submesh_index, u16 pass_index, u32 instance_count) = 0;
        virtual u16 DrawRenderer(Mesh *mesh, Material *material, const Matrix4x4f &world_mat, u16 submesh_index, u16 pass_index, u32 instance_count) = 0;
        virtual u16 DrawRenderer(Mesh *mesh, Material *material, const CBufferPerObjectData &per_obj_data, u16 submesh_index, u16 pass_index, u32 instance_count) = 0;
        virtual u16 DrawRenderer(Mesh *mesh, Material *material, u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh *mesh, Material *material, u32 instance_count, u16 pass_index) = 0;

        virtual void Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) = 0;
    };

    class AILU_API CommandBufferPool
    {
    public:
        static std::shared_ptr<CommandBuffer> Get(const String &name = "", ECommandBufferType type = ECommandBufferType::kCommandBufTypeDirect);
        static void Release(std::shared_ptr<CommandBuffer> &cmd);
        static void ReleaseAll();
        static void WaitForAllCommand();

    private:
        static void Init();
        // static void WatchCommandBufferState();
    private:
        inline static const u16 kInitialPoolSize = 10u;
        inline static u16 s_cur_pool_size = kInitialPoolSize;
        inline static std::vector<std::tuple<bool, std::shared_ptr<CommandBuffer>>> s_cmd_buffers{};
        inline static std::mutex _mutex;
    };
}// namespace Ailu


#endif// !COMMAND_BUFFER__
