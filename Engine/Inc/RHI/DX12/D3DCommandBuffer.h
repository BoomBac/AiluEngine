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
#include "Render/GraphicsPipelineStateObject.h"

namespace Ailu
{
	class D3DCommandBuffer
	{
		friend class D3DCommandBufferPool;
		friend class D3DContext;
	public:
        D3DCommandBuffer(uint32_t id) : _id(id) {};
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
        void SetPSO(GraphicsPipelineStateObject* pso);
        void SetRenderTarget(Ref<RenderTexture> color, Ref<RenderTexture> depth);
	private:
		uint32_t _id = 0u;
		inline static Vector4f _clear_color = { 0.3f, 0.2f, 0.4f, 1.0f };
		std::vector<std::function<void()>> _commands{};
	};

    class D3DCommandBufferPool 
    {
    public:
        static std::shared_ptr<D3DCommandBuffer> GetCommandBuffer() 
        {
            static bool s_b_init = false;
            if (!s_b_init)
            {
                Init();
                s_b_init = true;
            }
            std::unique_lock<std::mutex> lock(_mutex);
            for (auto& cmd : s_cmd_buffers) 
            {
                auto& [avairable, cmd_buf] = cmd;
                if (avairable) {
                    avairable = false;
                    return cmd_buf;
                }
            }
            int newId = (uint32_t)s_cmd_buffers.size();
            auto cmd = std::make_shared<D3DCommandBuffer>(newId);
            s_cmd_buffers.emplace_back(std::make_tuple(false, cmd));
            return cmd;
        }

        static void ReleaseCommandBuffer(std::shared_ptr<D3DCommandBuffer> cmd) 
        {
            std::unique_lock<std::mutex> lock(_mutex);
            std::get<0>(s_cmd_buffers[cmd->_id]) = true;
            cmd->Clear();
        }

    private:
        static void Init() 
        {
            for (int i = 0; i < kInitialPoolSize; i++)
            {
                auto cmd = std::make_shared<D3DCommandBuffer>(i);
                s_cmd_buffers.emplace_back(std::make_tuple(true, cmd));
            }
        }

    private:
        static inline std::vector<std::tuple<bool, std::shared_ptr<D3DCommandBuffer>>> s_cmd_buffers{};
        static inline std::mutex _mutex;
        static const int kInitialPoolSize = 10;
    };

}


#endif // !D3D_COMMAND_BUF_H__
