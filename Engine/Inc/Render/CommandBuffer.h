#pragma once
#ifndef __COMMAND_BUFFER__
#define __COMMAND_BUFFER__

#include <tuple>
#include <mutex>
#include "Framework/Math/ALMath.hpp"
#include "Texture.h"
#include "Material.h"
#include "Mesh.h"
#include "RendererAPI.h"


namespace Ailu
{
    class VertexBuffer;
    class IndexBuffer;
    class ConstantBuffer;
	class CommandBuffer
	{
	public:
        virtual ~CommandBuffer() = default;
        virtual void SubmitBindResource(void* res, const EBindResDescType& res_type, u8 slot = 255) = 0;
        virtual void Clear() = 0;
        virtual void Close() = 0;
        virtual u32 GetID() = 0;
        virtual const String& GetName() const = 0;
        virtual void SetName(const String& name) = 0;

        virtual void ClearRenderTarget(RenderTexture* color, RenderTexture* depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(Vector<RenderTexture*>& colors, RenderTexture* depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(RenderTexture* color, Vector4f clear_color,u16 index = 0u) = 0;
        virtual void ClearRenderTarget(RenderTexture* depth, float depth_value = 1.0f,u8 stencil_value = 0u) = 0;
        virtual void SetRenderTargets(Vector<RenderTexture*>& colors, RenderTexture* depth) = 0;
        virtual void SetRenderTarget(RenderTexture* color, RenderTexture* depth) = 0;
        /// <summary>
        /// 设置rendertarget，index用于cubemap或者texture array
        /// </summary>
        /// <param name="color"></param>
        /// <param name="index"></param>
        virtual void SetRenderTarget(RenderTexture*color,u16 index = 0u) = 0;

        virtual void ClearRenderTarget(RTHandle color, RTHandle depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(Vector<RTHandle>& colors, RTHandle depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(RTHandle color, Vector4f clear_color, u16 index = 0u) = 0;
        virtual void ClearRenderTarget(RTHandle depth, float depth_value = 1.0f, u8 stencil_value = 0u) = 0;
        virtual void SetRenderTargets(Vector<RTHandle>& colors, RTHandle depth) = 0;
        virtual void SetRenderTarget(RTHandle color, RTHandle depth) = 0;
        virtual void SetRenderTarget(RTHandle color, u16 index = 0u) = 0;

        virtual void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, u32 instance_count) = 0;
        virtual void DrawIndexedInstanced(u32 index_count, u32 instance_count) = 0;
        virtual void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, u32 instance_count) = 0;
        virtual void DrawInstanced(u32 vert_count,u32 instance_count) = 0;
        virtual void SetViewports(const std::initializer_list<Rect>& viewports) = 0;
        virtual void SetScissorRects(const std::initializer_list<Rect>& rects) = 0;
        virtual void SetViewport(const Rect& viewport) = 0;
        virtual void SetScissorRect(const Rect& rect) = 0;
        virtual u16 DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh* mesh, Material* material, const Matrix4x4f& transform, u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh* mesh, Material* material, ConstantBuffer* per_obj_cbuf, u16 submesh_index,u32 instance_count = 1u) = 0;
        virtual u16 DrawRenderer(Mesh* mesh, Material* material,u32 instance_count = 1u) = 0;
        virtual void ResolveToBackBuffer(RenderTexture* color) = 0;
        virtual void ResolveToBackBuffer(RenderTexture* color, RenderTexture* destination) = 0;
        virtual void ResolveToBackBuffer(RTHandle color) = 0;
        virtual void ResolveToBackBuffer(RTHandle color, RTHandle destination) = 0;
        virtual void Dispatch(ComputeShader* cs,u16 thread_group_x, u16 thread_group_y, u16 thread_group_z) = 0;
	};

    class CommandBufferPool
    {
    public:
        static std::shared_ptr<CommandBuffer> Get(const String& name = "");
        static void Release(std::shared_ptr<CommandBuffer>& cmd);
        static void ReleaseAll();
    private:
        static void Init();
       // static void WatchCommandBufferState();
    private:
        inline static const u16 kInitialPoolSize = 10u;
        inline static u16 s_cur_pool_size = kInitialPoolSize;
        inline static std::vector<std::tuple<bool, std::shared_ptr<CommandBuffer>>> s_cmd_buffers{};
        inline static std::mutex _mutex;
    };
}


#endif // !COMMAND_BUFFER__

