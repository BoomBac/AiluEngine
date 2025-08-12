//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/UIRenderer.h"
#include "UI/Canvas.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include <Framework/Common/ResourceMgr.h>
#include <Framework/Common/Allocator.hpp>

namespace Ailu
{
    namespace UI
    {
        using namespace Render;
        static UIRenderer* s_Renderer = nullptr;
        void UIRenderer::Init()
        {
            s_Renderer = AL_NEW(UIRenderer);
        }
        void UIRenderer::Shutdown()
        {
            AL_DELETE(s_Renderer);
        }
        UIRenderer *UIRenderer::Get()
        {
            return s_Renderer;
        }
        void UIRenderer::DrawQuad(Vector2f position, Vector2f size,f32 depth, Color color)
        {
            DrawerBlock *available_block = nullptr;
            auto &frame_block = s_Renderer->FrameBlocks();
            for (auto b: frame_block)
            {
                if (b->_cur_vert_num + 6 <= b->_max_vert_num)
                {
                    available_block = b;
                    break;
                }
            }
            if (available_block == nullptr)
            {
                frame_block.push_back(AL_NEW(DrawerBlock));
                available_block = frame_block.back();
            }
            auto &cb = available_block;
            cb->_pos_buf[cb->_cur_vert_num  ] = {position, depth};
            cb->_pos_buf[cb->_cur_vert_num+1] = {position.x + size.x, position.y, depth};
            cb->_pos_buf[cb->_cur_vert_num+2] = {position.x, position.y - size.y, depth};
            cb->_pos_buf[cb->_cur_vert_num+3] = {position.x + size.x, position.y - size.y, depth};
            cb->_uv_buf[cb->_cur_vert_num  ] = {0.f, 0.f};
            cb->_uv_buf[cb->_cur_vert_num+1] = {1.f, 0.f};
            cb->_uv_buf[cb->_cur_vert_num+2] = {0.f, 1.f};
            cb->_uv_buf[cb->_cur_vert_num+3] = {1.f, 1.f};
            cb->_color_buf[cb->_cur_vert_num  ] = color;
            cb->_color_buf[cb->_cur_vert_num+1] = color;
            cb->_color_buf[cb->_cur_vert_num+2] = color;
            cb->_color_buf[cb->_cur_vert_num+3] = color;
            cb->_index_buf[cb->_cur_index_num] = 0;
            cb->_index_buf[cb->_cur_index_num+1] = 1;
            cb->_index_buf[cb->_cur_index_num+2] = 2;
            cb->_index_buf[cb->_cur_index_num+3] = 1;
            cb->_index_buf[cb->_cur_index_num+4] = 3;
            cb->_index_buf[cb->_cur_index_num+5] = 2;
            cb->_cur_vert_num += 4;
            cb->_cur_index_num += 6;
        }
        UIRenderer::UIRenderer()
        {
            _obj_cb.reset(ConstantBuffer::Create(Render::RenderConstants::kPerObjectDataSize));
        }
        UIRenderer::~UIRenderer()
        {
            for(auto& frame_blocks : _drawer_blocks)
            {
                for (auto b: frame_blocks)
                {
                    AL_DELETE(b);
                }
            }
            for (auto* c: _canvases)
            {
                AL_DELETE(c);
            }
        }

        void UIRenderer::Render(CommandBuffer *cmd)
        {
            for (auto *canvas: _active_canvases)
            {
                canvas->Update();
                canvas->Render(*this);
                auto [color,depth] = canvas->GetOutput();
                f32 w = (f32) color->Width();
                f32 h = (f32) color->Height();
                //cmd->ClearRenderTarget(color,depth,Colors::kBlack,kZFar);
                CBufferPerCameraData cb_per_cam;
                cb_per_cam._MatrixVP = Camera::GetDefaultOrthogonalViewProj(w, h);
                cb_per_cam._ScreenParams = Vector4f(1.0f / w, 1.0f / h, w, h);
                CBufferPerObjectData per_obj_data;
                per_obj_data._MatrixWorld = MatrixTranslation(-w * 0.5f, h * 0.5f, 0.f);
                memcpy(_obj_cb->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &cb_per_cam, RenderConstants::kPerCameraDataSize);
                cmd->SetRenderTarget(color, depth);
                Color tint = Colors::kWhite;
                for (auto *b: _drawer_blocks[_frame_index])
                {
                    if (b->_cur_vert_num == 0)
                        break;
                    b->_mat->SetVector("_Color", tint);
                    b->_vbuf->SetData((u8 *) b->_pos_buf.data(), b->_cur_vert_num * sizeof(Vector3f), 0u, 0u);
                    b->_vbuf->SetData((u8 *) b->_uv_buf.data(), b->_cur_vert_num * sizeof(Vector2f), 1u, 0u);
                    b->_vbuf->SetData((u8 *) b->_color_buf.data(), b->_cur_vert_num * sizeof(Vector4f), 2u, 0u);
                    b->_ibuf->SetData((u8 *) b->_index_buf.data(), b->_cur_index_num * sizeof(u32));
                    cmd->DrawIndexed(b->_vbuf, b->_ibuf, _obj_cb.get(), b->_mat.get());
                    b->_cur_index_num = 0u;
                    b->_cur_vert_num = 0u;
                }
            }
        }

        Canvas *UIRenderer::AddCanvas()
        {
            _canvases.emplace_back(AL_NEW(Canvas));
            _active_canvases.emplace_back(_canvases.back());
            _canvases.back()->Name(std::format("Canvas_{}", _canvases.size()));
            return _canvases.back();
        }

        void UIRenderer::RemoveCanvas(Canvas *canvas)
        {
            std::erase_if(_active_canvases, [&](Canvas *c){ return c == canvas; });
        }

        void UIRenderer::DeleteCanvas(Canvas *canvas)
        {
            std::erase_if(_canvases, [&](Canvas *c){ return c == canvas; });
            RemoveCanvas(canvas);
            AL_DELETE(canvas);
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
        UIRenderer::DrawerBlock::DrawerBlock(u32 vert_num) : _max_vert_num(vert_num)
        {
            Vector<VertexBufferLayoutDesc> desc_list;
            desc_list.emplace_back("POSITION", EShaderDateType::kFloat3, 0);
            desc_list.emplace_back("TEXCOORD", EShaderDateType::kFloat2, 1);
            desc_list.emplace_back(RenderConstants::kSemanticColor, EShaderDateType::kFloat4, 2);
            _vbuf = VertexBuffer::Create(desc_list, "ui_vbuf");
            _ibuf = IndexBuffer::Create(nullptr, vert_num, "ui_ibuf", true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector3f), 0, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector2f), 1, true);
            _vbuf->SetStream(nullptr, vert_num * sizeof(Vector4f), 2, true);
            _mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_ui.alasset"), "DefaultUIMaterial");
            _mat->SetTexture("_MainTex", Texture::s_p_default_white);
            _mat->SetVector("_Color", Colors::kWhite);
            _pos_buf.resize(vert_num);
            _uv_buf.resize(vert_num);
            _color_buf.resize(vert_num);
            _index_buf.resize(vert_num);
            GraphicsContext::Get().CreateResource(_vbuf);
            GraphicsContext::Get().CreateResource(_ibuf);
        }

        UIRenderer::DrawerBlock::~DrawerBlock()
        {
            DESTORY_PTR(_vbuf);
            DESTORY_PTR(_ibuf);
        }
    }// namespace UI
}// namespace Aliu