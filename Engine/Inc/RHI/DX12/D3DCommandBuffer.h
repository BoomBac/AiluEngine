#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__

#include "Render/CommandBuffer.h"

namespace Ailu
{
	class D3DCommandBuffer : public CommandBuffer
	{
	public:
        D3DCommandBuffer() = default;
        D3DCommandBuffer(uint32_t id) : _id(id) {};

        void SetClearColor(const Vector4f& color) final;
        void Clear() final;
        void ClearRenderTarget(Vector4f color, float depth, bool clear_color, bool clear_depth) final;
        void ClearRenderTarget(Ref<RenderTexture> color, Ref<RenderTexture> depth, Vector4f clear_color, float clear_depth) final;
        void ClearRenderTarget(Ref<RenderTexture>& color, Vector4f clear_color) final;
        void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, uint32_t instance_count) final;
        void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count) final;
        void SetViewMatrix(const Matrix4x4f& view) final;
        void SetProjectionMatrix(const Matrix4x4f& proj) final;
        void SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj) final;
        void SetViewports(const std::initializer_list<Rect>& viewports) final;
        void SetScissorRects(const std::initializer_list<Rect>& rects) final;
        void SetViewport(const Rect& viewport) final;
        void SetScissorRect(const Rect& rect) final;
        void DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform, const Ref<Material>& material, uint32_t instance_count = 1u) final;
        void DrawRenderer(Mesh* mesh, Material* material,const Matrix4x4f& transform, uint32_t instance_count = 1u) final;
        void SetPSO(GraphicsPipelineStateObject* pso) final;
        void SetRenderTarget(Ref<RenderTexture>& color, Ref<RenderTexture>& depth) final;
        void SetRenderTarget(Ref<RenderTexture>& color) final;
        void ResolveToBackBuffer(Ref<RenderTexture>& color) final;
        void ResolveToBackBuffer(RenderTexture* color) final;

        Vector<std::function<void()>>& GetAllCommands() final{ return _commands; }
        virtual u32 GetID() final { return _id; };
	private:
		uint32_t _id = 0u;
		inline static Vector4f _clear_color = { 0.3f, 0.2f, 0.4f, 1.0f };
		Vector<std::function<void()>> _commands{};
	};
}


#endif // !D3D_COMMAND_BUF_H__
