#include "pch.h"
#include "Render/RenderCommand.h"

namespace Ailu
{
	void RenderCommand::SetClearColor(const Vector4f& color)
	{
		s_p_renderer_api->SetClearColor(color);
	}

	void RenderCommand::Clear()
	{
		s_p_renderer_api->Clear();
	}

	void RenderCommand::DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, uint32_t instance_count, Matrix4x4f transform)
	{
		s_p_renderer_api->DrawIndexedInstanced(index_buffer, transform, instance_count);
	}

	void RenderCommand::DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, uint32_t instance_count, Matrix4x4f transform)
	{
		s_p_renderer_api->DrawInstanced(vertex_buf, transform, instance_count);
	}

	void RenderCommand::SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj)
	{
		s_p_renderer_api->SetViewProjectionMatrices(view, proj);
	}

	void RenderCommand::SetProjectionMatrix(const Matrix4x4f& proj)
	{
		s_p_renderer_api->SetProjectionMatrix(proj);
	}

	void RenderCommand::SetViewMatrix(const Matrix4x4f& view)
	{
		s_p_renderer_api->SetViewMatrix(view);
	}
	void Ailu::RenderCommand::SetViewports(const std::initializer_list<Viewport>& viewports)
	{
		s_p_renderer_api->SetViewports(viewports);
	}
	void RenderCommand::SetScissorRects(const std::initializer_list<Viewport>& rects)
	{
		s_p_renderer_api->SetScissorRects(rects);
	}
}
