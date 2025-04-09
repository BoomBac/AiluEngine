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

    class RHICommandBuffer : public Object
    {
    public:
        explicit RHICommandBuffer(String name,ECommandBufferType type = ECommandBufferType::kCommandBufTypeDirect) : _cmd_type(type)
        {
            _name = std::move(name);
        };
        virtual ~RHICommandBuffer(){};
        virtual void Clear(){};
        virtual bool IsReady() const { return true; }
        [[nodiscard]] ECommandBufferType  GetCommandBufferType() const { return _cmd_type; }
        [[nodiscard]] bool IsExecuted() const { return _is_executed; }
    protected:
        bool _is_executed;
    private:
        ECommandBufferType _cmd_type;
    };
    class RHICommandBufferPool
    {
    public:
        static void Init();
        static void Shutdown();
        static Ref<RHICommandBuffer> Get(const String &name = "", ECommandBufferType type = ECommandBufferType::kCommandBufTypeDirect);
        static void Release(Ref<RHICommandBuffer> &cmd);

    private:
        inline static const u16 kInitialPoolSize = 10u;
        u16 _cur_pool_size = kInitialPoolSize;
        std::vector<std::tuple<bool, Ref<RHICommandBuffer>>> _cmd_buffers{};
        std::mutex _mutex;
    };

    class VertexBuffer;
    class IndexBuffer;
    class ConstantBuffer;
    struct GfxCommand;
    class AILU_API CommandBuffer : public Object
    {
        friend class D3DContext;
    public:
        CommandBuffer(String name);
        ~CommandBuffer() override = default;
        void Clear();
        void ClearRenderTarget(Color color,f32 depth,u8 stencil);
        void ClearRenderTarget(Color c);
        void ClearRenderTarget(f32 depth, u8 stencil);
        void ClearRenderTarget(Color* colors,u16 num,f32 depth,u8 stencil);
        void ClearRenderTarget(Color* colors, u16 num);

        void SetRenderTarget(RenderTexture *color, RenderTexture *depth);
        /// <summary>
        /// 设置rendertarget，index用于cubemap或者texture array
        /// </summary>
        /// <param name="color"></param>
        /// <param name="index"></param>
        void SetRenderTarget(RenderTexture *color, u16 index = 0u);
        void SetRenderTarget(RenderTexture *color, RenderTexture *depth, u16 color_index, u16 depth_index);
        void SetRenderTargets(Vector<RTHandle> &colors, RTHandle depth);
        void SetRenderTarget(RTHandle color, RTHandle depth);
        void SetRenderTarget(RTHandle color, u16 index = 0u);
        /// @brief 立即生效
        /// @param tex 
        /// @param action 
        void SetRenderTargetLoadAction(RenderTexture* tex, ELoadStoreAction action);
        /// @brief 立即生效
        /// @param tex 
        /// @param action 
        void SetRenderTargetLoadAction(RTHandle handle, ELoadStoreAction action);

        void DrawIndexed(VertexBuffer *vb, IndexBuffer *ib, ConstantBuffer *per_obj_cb, Material *mat, u16 pass_index = 0u);
        void DrawInstanced(VertexBuffer *vb, ConstantBuffer *per_obj_cb,Material*mat,u16 pass_index,u16 instance_count);

        void SetViewport(const Rect &viewport);
        void SetScissorRect(const Rect &rect);
        void SetViewports(const std::initializer_list<Rect> &viewports);
        void SetScissorRects(const std::initializer_list<Rect> &rects);

        RTHandle GetTempRT(u16 width, u16 height, String name, ERenderTargetFormat::ERenderTargetFormat format, bool mipmap_chain, bool linear, bool random_access);
        void ReleaseTempRT(RTHandle handle);

        void Blit(RTHandle src, RTHandle dst, Material *mat = nullptr, u16 pass_index = 0u);
        void Blit(RenderTexture *src, RenderTexture *dst, Material *mat = nullptr, u16 pass_index = 0u);
        void Blit(RenderTexture *src, RenderTexture *dst, u16 src_view_index, u16 dst_view_index, Material *mat = nullptr, u16 pass_index = 0u);
        void Blit(RTHandle src, RenderTexture *dst, Material *mat = nullptr, u16 pass_index = 0u);
        void Blit(RenderTexture *src, RTHandle dst, Material *mat = nullptr, u16 pass_index = 0u);

        void DrawFullScreenQuad(Material *mat, u16 pass_index = 0u);

        void SetGlobalBuffer(const String &name, void *data, u64 data_size);
        void SetGlobalBuffer(const String &name, ConstantBuffer *buffer);
        void SetGlobalBuffer(const String &name, GPUBuffer *buffer);
        void SetComputeBuffer(const String &name, u16 kernel,void *data, u64 data_size);
        void SetGlobalTexture(const String &name, Texture *tex);
        void SetGlobalTexture(const String &name, RTHandle handle);

        void DrawMesh(Mesh *mesh, Material *material, const Matrix4x4f &world_matrix, u16 sub_mesh = 0u, u32 instance_count = 1u);

        void DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cb, u32 instance_count = 1u);
        void DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cb, u16 sub_mesh, u32 instance_count = 1u);
        void DrawRenderer(Mesh *mesh, Material *material, ConstantBuffer *per_obj_cb, u16 sub_mesh, u16 pass_index, u32 instance_count);
        void DrawRenderer(Mesh *mesh, Material *material, const Matrix4x4f &world_mat, u16 sub_mesh, u16 pass_index, u32 instance_count);
        void DrawRenderer(Mesh *mesh, Material *material, const CBufferPerObjectData &per_obj_data, u16 sub_mesh, u16 pass_index, u32 instance_count);

        void Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y);
        void Dispatch(ComputeShader *cs, u16 kernel, u16 thread_group_x, u16 thread_group_y, u16 thread_group_z);

        void BeginProfiler(const String &name);
        void EndProfiler();

        void StateTransition(GpuResource* res,EResourceState new_state,u32 sub_res = kTotalSubRes);
    private:
        Vector<GfxCommand *> _commands;
        Array<Rect, RenderConstants::kMaxMRTNum> _viewports;
    };

    class AILU_API CommandBufferPool
    {
    public:
        static void Init();
        static void Shutdown();
        static Ref<CommandBuffer> Get(const String &name = "");
        static void Release(Ref<CommandBuffer> &cmd);
        static void ReleaseAll();

    private:
        inline static const u16 kInitialPoolSize = 10u;
        u16 _cur_pool_size = kInitialPoolSize;
        std::vector<std::tuple<bool, Ref<CommandBuffer>>> _cmd_buffers{};
        std::mutex _mutex;
    };
}// namespace Ailu


#endif// !COMMAND_BUFFER__
