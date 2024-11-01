//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/UIRenderer.h"
#include <Framework/Common/ResourceMgr.h>
#include "Render/CommandBuffer.h"
#include "Framework/Common/Profiler.h"

namespace Ailu
{
    namespace UI
    {
        static UIRenderer* s_Renderer = nullptr;
        void UIRenderer::Init()
        {
            s_Renderer = new UIRenderer();
        }
        void UIRenderer::Shutdown()
        {
            DESTORY_PTR(s_Renderer);
        }
        UIRenderer *UIRenderer::Get()
        {
            return s_Renderer;
        }
        void UIRenderer::DrawQuad(Vector2f position, Vector2f size,f32 depth, Color color)
        {
            auto &b = s_Renderer->_drawer_blocks[s_Renderer->_frame_index];
            if (!b.contains(color))
            {
                b[color] = new UIRenderer::DrawerBlock();
            }
            auto &cb = b[color];
            cb->_pos_buf[cb->_pos_offset++] = {position,depth};
            cb->_pos_buf[cb->_pos_offset++] = {position.x + size.x, position.y, depth};
            cb->_pos_buf[cb->_pos_offset++] = {position.x, position.y - size.y, depth};
            cb->_pos_buf[cb->_pos_offset++] = {position.x + size.x, position.y - size.y, depth};
            cb->_uv_buf[cb->_uv_offset++] = {0.f, 0.f};
            cb->_uv_buf[cb->_uv_offset++] = {1.f, 0.f};
            cb->_uv_buf[cb->_uv_offset++] = {0.f, 1.f};
            cb->_uv_buf[cb->_uv_offset++] = {1.f, 1.f};
            cb->_index_buf[cb->_index_offset++] = 0;
            cb->_index_buf[cb->_index_offset++] = 1;
            cb->_index_buf[cb->_index_offset++] = 2;
            cb->_index_buf[cb->_index_offset++] = 1;
            cb->_index_buf[cb->_index_offset++] = 3;
            cb->_index_buf[cb->_index_offset++] = 2;
        }
        UIRenderer::UIRenderer()
        {
            _obj_cb.reset(IConstantBuffer::Create(RenderConstants::kPerObjectDataSize));
        }
        UIRenderer::~UIRenderer()
        {
            for(auto& b : _drawer_blocks)
            {
                for(auto& [color, block] : b)
                {
                    DESTORY_PTR(block);
                }
            }
        }
        void UIRenderer::Render(RTHandle color, RTHandle depth)
        {
            Render(g_pRenderTexturePool->Get(color), g_pRenderTexturePool->Get(depth));
        }
        void UIRenderer::Render(RTHandle color, RTHandle depth, CommandBuffer *cmd)
        {
            Render(g_pRenderTexturePool->Get(color), g_pRenderTexturePool->Get(depth),cmd);
        }

        void UIRenderer::Render(RenderTexture *color, RenderTexture *depth)
        {
            auto cmd = CommandBufferPool::Get("UI");
            {
                PROFILE_BLOCK_GPU(cmd.get(), UI);
                Render(color, depth, cmd.get());
            }
            g_pGfxContext->ExecuteCommandBuffer(cmd);
            CommandBufferPool::Release(cmd);
        }

        void UIRenderer::Render(RenderTexture *color, RenderTexture *depth, CommandBuffer *cmd)
        {
            f32 w = (f32) color->Width();
            f32 h = (f32) color->Height();
            cmd->ClearRenderTarget(color,depth,Colors::kBlack,1.0f);
            CBufferPerCameraData cb_per_cam;
            cb_per_cam._MatrixVP = Camera::GetDefaultOrthogonalViewProj(w, h);
            cb_per_cam._ScreenParams = Vector4f(w, h, 1 / w, 1 / h);
            CBufferPerObjectData per_obj_data;
            per_obj_data._MatrixWorld = MatrixTranslation(-w*0.5f,h*0.5f,0.f);
            memcpy(_obj_cb->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &cb_per_cam, RenderConstants::kPerCameraDataSize);
            cmd->SetRenderTarget(color, depth);
            for (auto &it: _drawer_blocks[_frame_index])
            {
                auto &[color, b] = it;
                if (b->_index_offset == 0)
                    continue;
                b->_mat->SetVector("_Color", color);
                b->_vbuf->SetData((u8 *) b->_pos_buf.data(), b->_pos_offset * sizeof(Vector3f), 0u, 0u);
                b->_vbuf->SetData((u8 *) b->_uv_buf.data(), b->_uv_offset * sizeof(Vector2f), 1u, 0u);
                b->_ibuf->SetData((u8 *) b->_index_buf.data(), b->_index_offset * sizeof(u32));
                cmd->DrawIndexed(b->_vbuf, b->_ibuf, _obj_cb.get(), b->_mat.get());
                b->_pos_offset = 0u;
                b->_uv_offset = 0u;
                b->_index_offset = 0u;
            }
        }

        UIRenderer::DrawerBlock::DrawerBlock(UIRenderer::DrawerBlock &&other) noexcept
        {
            _vbuf = other._vbuf;
            _ibuf = other._ibuf;
            _mat = std::move(other._mat);
            other._vbuf = nullptr;
            other._ibuf = nullptr;
        }
        UIRenderer::DrawerBlock &UIRenderer::DrawerBlock::operator=(UIRenderer::DrawerBlock &&other) noexcept
        {
            _vbuf = other._vbuf;
            _ibuf = other._ibuf;
            _mat = std::move(other._mat);
            other._vbuf = nullptr;
            other._ibuf = nullptr;
            return *this;
        }
        UIRenderer::DrawerBlock::DrawerBlock(u32 vert_num)
        {
            Vector<VertexBufferLayoutDesc> desc_list;
            desc_list.emplace_back("POSITION", EShaderDateType::kFloat3, 0);
            desc_list.emplace_back("TEXCOORD", EShaderDateType::kFloat2, 1);
            _vbuf = IVertexBuffer::Create(desc_list, "ui_vbuf");
            _ibuf = IIndexBuffer::Create(nullptr, vert_num, "ui_ibuf", true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector3f), 0, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector2f), 1, true);
            _mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_ui.alasset"), "DefaultUIMaterial");
            _mat->SetTexture("_MainTex", Texture::s_p_default_white);
            _mat->SetVector("_Color", Colors::kWhite);
            _pos_buf.resize(vert_num);
            _uv_buf.resize(vert_num);
            _index_buf.resize(vert_num);
        }

        UIRenderer::DrawerBlock::~DrawerBlock()
        {
            DESTORY_PTR(_vbuf);
            DESTORY_PTR(_ibuf);
        }
    }// namespace UI
}// namespace Aliu