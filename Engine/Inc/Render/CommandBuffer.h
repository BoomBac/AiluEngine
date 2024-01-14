#pragma once
#ifndef __COMMAND_BUFFER__
#define __COMMAND_BUFFER__

#include <functional>
#include <tuple>
#include <mutex>
#include "Framework/Math/ALMath.hpp"
#include "Texture.h"
#include "Buffer.h"
#include "Material.h"
#include "GraphicsPipelineStateObject.h"
#include "RendererAPI.h"
#include "Mesh.h"


namespace Ailu
{
	class CommandBuffer
	{
	public:
        virtual ~CommandBuffer() = default;

        virtual void SetClearColor(const Vector4f& color) = 0;
        virtual void Clear() = 0;
        virtual void ClearRenderTarget(Vector4f color, float depth, bool clear_color, bool clear_depth) = 0;
        virtual void ClearRenderTarget(Ref<RenderTexture> color, Ref<RenderTexture> depth, Vector4f clear_color, float clear_depth) = 0;
        virtual void ClearRenderTarget(Ref<RenderTexture>& color, Vector4f clear_color) = 0;
        virtual void ClearRenderTarget(RenderTexture* depth, float depth_value = 1.0f,u8 stencil_value = 0u) = 0;
        virtual void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, uint32_t instance_count) = 0;
        virtual void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count) = 0;
        virtual void SetViewMatrix(const Matrix4x4f& view) = 0;
        virtual void SetProjectionMatrix(const Matrix4x4f& proj) = 0;
        virtual void SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj) = 0;
        virtual void SetShadowMatrix(const Matrix4x4f& shadow_matrix,u16 index) = 0;
        virtual void SetViewports(const std::initializer_list<Rect>& viewports) = 0;
        virtual void SetScissorRects(const std::initializer_list<Rect>& rects) = 0;
        virtual void SetViewport(const Rect& viewport) = 0;
        virtual void SetScissorRect(const Rect& rect) = 0;
        virtual void DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, uint32_t instance_count = 1u) = 0;
        virtual void DrawRenderer(Mesh* mesh, Material* material, const Matrix4x4f& transform, uint32_t instance_count = 1u) = 0;
        virtual void SetPSO(GraphicsPipelineStateObject* pso) = 0;
        virtual void SetRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth) = 0;
        virtual void SetRenderTarget(RenderTexture* color, RenderTexture* depth) = 0;
        virtual void SetRenderTarget(Ref<RenderTexture>& color) = 0;
        virtual void ResolveToBackBuffer(Ref<RenderTexture>& color) = 0;
        virtual void ResolveToBackBuffer(RenderTexture* color) = 0;

        virtual Vector<std::function<void()>>& GetAllCommands() = 0;
        virtual u32 GetID() = 0;
	};

    class CommandBufferPool
    {
    public:
        static std::shared_ptr<CommandBuffer> GetCommandBuffer();
        static void ReleaseCommandBuffer(std::shared_ptr<CommandBuffer> cmd);
    private:
        static void Init();
    private:
        static inline std::vector<std::tuple<bool, std::shared_ptr<CommandBuffer>>> s_cmd_buffers{};
        static inline std::mutex _mutex;
        static const int kInitialPoolSize = 10;
    };
}


#endif // !COMMAND_BUFFER__

