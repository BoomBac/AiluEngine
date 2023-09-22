#pragma once
#ifndef __D3D_COMMAND_BUF_H__
#define __D3D_COMMAND_BUF_H__
#include <functional>
#include "GlobalMarco.h"
#include "Framework/Math/ALMath.hpp"
#include "Render/Buffer.h"
#include "Render/RendererAPI.h"
#include "Render/Material.h"
#include "Framework/Assets/Mesh.h"

namespace Ailu
{
	class D3DComandBuffer
	{
		friend class D3DComandBufferPool;
		friend class D3DContext;
	public:
		void SetClearColor(const Vector4f& color);
		void Clear();
		void ClearRenderTarget(Vector4f color, float depth, bool clear_color, bool clear_depth);
		void DrawIndexedInstanced(const std::shared_ptr<IndexBuffer>& index_buffer, const Matrix4x4f& transform, uint32_t instance_count);
		void DrawInstanced(const std::shared_ptr<VertexBuffer>& vertex_buf, const Matrix4x4f& transform, uint32_t instance_count);
		void SetViewMatrix(const Matrix4x4f& view);
		void SetProjectionMatrix(const Matrix4x4f& proj);
		void SetViewProjectionMatrices(const Matrix4x4f& view, const Matrix4x4f& proj);
		void SetViewports(const std::initializer_list<Viewport>& viewports);
		void SetScissorRects(const std::initializer_list<Viewport>& rects);
		void DrawRenderer(const Ref<Mesh>& mesh, const Matrix4x4f& transform,const Ref<Material>& material,uint32_t instance_count = 1u);
	private:
		uint32_t _id = 0u;
		inline static Vector4f _clear_color = { 0.3f, 0.2f, 0.4f, 1.0f };
		std::vector<std::function<void()>> _commands{};
	};

	class D3DComandBufferPool
	{
	public:
		static Ref<D3DComandBuffer>& GetCommandBuffer()
		{
			Init();
			for (auto& cmd : s_cmd_buffers)
			{
				auto& [active,cmd_buf] = cmd;
				if (active)
				{
					active = false;
					return cmd_buf;
				}
			}
			AL_ASSERT(true, "D3dcommandBufferPool is full!");
		}
		static void ReleaseCommandBuffer(Ref<D3DComandBuffer>& cmd)
		{					
			std::get<0>(s_cmd_buffers[cmd->_id]) = true;
			std::get<1>(s_cmd_buffers[cmd->_id])->Clear();
		}

		static std::vector<std::tuple<bool,Ref<D3DComandBuffer>>>& GetAllBuffer()
		{
			return s_cmd_buffers;
		}
	private:
		static void Init()
		{
			if (s_b_init) return;
			for (uint32_t i = 0; i < 10; i++)
			{
				auto cmd = MakeRef<D3DComandBuffer>();
				cmd->_id = i;
				s_cmd_buffers.emplace_back(std::make_pair(true,std::move(cmd)));
			}
			s_b_init = true;
		}
	private:
		inline static std::vector<std::tuple<bool, Ref<D3DComandBuffer>>> s_cmd_buffers{};
		inline static bool s_b_init = false;
	};
}


#endif // !D3D_COMMAND_BUF_H__
