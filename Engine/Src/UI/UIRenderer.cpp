//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/UIRenderer.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include "UI/TextRenderer.h"
#include "Render/Gizmo.h"
#include "UI/Widget.h"
#include <Framework/Common/Allocator.hpp>
#include <Framework/Common/ResourceMgr.h>

namespace Ailu
{
    namespace UI
    {
        using namespace Render;
        static UIRenderer *s_Renderer = nullptr;
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
        UIRenderer::UIRenderer()
        {
            _obj_cb.reset(ConstantBuffer::Create(Render::RenderConstants::kPerObjectDataSize));
        }
        UIRenderer::~UIRenderer()
        {
            for (auto &frame_blocks: _drawer_blocks)
            {
                for (auto b: frame_blocks)
                {
                    AL_DELETE(b);
                }
            }
            _widgets.clear();
        }

        void UIRenderer::Render(CommandBuffer *cmd)
        {
            std::stable_sort(_active_widgets.begin(), _active_widgets.end(), [](Ref<Widget> a, Ref<Widget> b) -> bool
                             { return *a < *b; });
            _cur_widget_index = 0u;
            for (auto &canvas: _active_widgets)
            {
                canvas->Update();
                canvas->Render(*this);
                auto b = _drawer_blocks[_frame_index][_cur_widget_index];
                if (b->_cur_vert_num == 0u)
                    continue;

                auto [color, depth] = canvas->GetOutput();
                f32 w = (f32) color->Width();
                f32 h = (f32) color->Height();
                //cmd->ClearRenderTarget(color,depth,Colors::kBlack,kZFar);
                CBufferPerCameraData cb_per_cam;

                Matrix4x4f view, proj;
                f32 aspect = w / h;
                f32 half_width = w * 0.5f, half_height = h * 0.5f;
                BuildViewMatrixLookToLH(view, Vector3f(0.f, 0.f, -50.f), Vector3f::kForward, Vector3f::kUp);
                BuildOrthographicMatrix(proj, 0.0f, w, 0.0f, h, 1.f, 200.f);

                cb_per_cam._MatrixVP = view * proj;
                cb_per_cam._ScreenParams = Vector4f(1.0f / w, 1.0f / h, w, h);
                CBufferPerObjectData per_obj_data;
                per_obj_data._MatrixWorld = BuildIdentityMatrix();
                memcpy(_obj_cb->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
                cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &cb_per_cam, RenderConstants::kPerCameraDataSize);
                cmd->SetRenderTarget(color, depth);
                Color tint = Colors::kWhite;
                b->_mat->SetVector("_Color", tint);
                b->_vbuf->SetData((u8 *) b->_pos_buf.data(), b->_cur_vert_num * sizeof(Vector3f), 0u, 0u);
                b->_vbuf->SetData((u8 *) b->_uv_buf.data(), b->_cur_vert_num * sizeof(Vector2f), 1u, 0u);
                b->_vbuf->SetData((u8 *) b->_color_buf.data(), b->_cur_vert_num * sizeof(Vector4f), 2u, 0u);
                b->_ibuf->SetData((u8 *) b->_index_buf.data(), b->_cur_index_num * sizeof(u32));
                cmd->DrawIndexed(b->_vbuf, b->_ibuf, _obj_cb.get(), b->_mat.get());
                b->_cur_index_num = 0u;
                b->_cur_vert_num = 0u;
                Render::TextRenderer::Get()->Render(color,cmd);
                ++_cur_widget_index;
            }
            static RenderingData data;
            data._width =  RenderTexture::s_backbuffer->Width();
            data._height = RenderTexture::s_backbuffer->Height();
            //TODO:remove
            Render::Gizmo::Submit(cmd, data);
        }

        void UIRenderer::DrawQuad(Vector2f position, Vector2f size, f32 depth, Color color)
        {
            DrawerBlock *cb = GetAvailableBlock(6u);
            cb->_pos_buf[cb->_cur_vert_num] = {position, depth};
            cb->_pos_buf[cb->_cur_vert_num + 1] = {position.x + size.x, position.y, depth};
            cb->_pos_buf[cb->_cur_vert_num + 2] = {position.x, position.y + size.y, depth};
            cb->_pos_buf[cb->_cur_vert_num + 3] = {position.x + size.x, position.y + size.y, depth};
            cb->_uv_buf[cb->_cur_vert_num] = {0.f, 0.f};
            cb->_uv_buf[cb->_cur_vert_num + 1] = {1.f, 0.f};
            cb->_uv_buf[cb->_cur_vert_num + 2] = {0.f, 1.f};
            cb->_uv_buf[cb->_cur_vert_num + 3] = {1.f, 1.f};
            cb->_color_buf[cb->_cur_vert_num] = color;
            cb->_color_buf[cb->_cur_vert_num + 1] = color;
            cb->_color_buf[cb->_cur_vert_num + 2] = color;
            cb->_color_buf[cb->_cur_vert_num + 3] = color;
            cb->_index_buf[cb->_cur_index_num] =     cb->_cur_vert_num + 0u;
            cb->_index_buf[cb->_cur_index_num + 1] = cb->_cur_vert_num + 1u;
            cb->_index_buf[cb->_cur_index_num + 2] = cb->_cur_vert_num + 2u;
            cb->_index_buf[cb->_cur_index_num + 3] = cb->_cur_vert_num + 1u;
            cb->_index_buf[cb->_cur_index_num + 4] = cb->_cur_vert_num + 3u;
            cb->_index_buf[cb->_cur_index_num + 5] = cb->_cur_vert_num + 2u;
            cb->_cur_vert_num += 4;
            cb->_cur_index_num += 6;
        }

        void UIRenderer::DrawQuad(Vector4f rect, f32 depth, Color color)
        {
            DrawQuad({rect.x, rect.y}, {rect.z, rect.w}, depth, color);
        }

        void UIRenderer::DrawText(const String &text, Vector2f pos,u16 font_size,Vector2f scale,Color color,Render::Font *font)
        {
            Render::TextRenderer::Get()->DrawText(text,pos,font_size,scale,color,font);
        }

        void UIRenderer::DrawLine(Vector2f a, Vector2f b, f32 thickness, Color color, f32 depth)
        {
            // 线方向
            Vector2f dir = Normalize(b - a);
            // 法线（垂直方向）
            Vector2f normal = {-dir.y, dir.x};
            Vector2f offset = normal * (thickness * 0.5f);

            // 矩形的四个点
            Vector2f p0 = a - offset;
            Vector2f p1 = b - offset;
            Vector2f p2 = a + offset;
            Vector2f p3 = b + offset;

            DrawerBlock *cb = GetAvailableBlock(6u);
            auto v = cb->_cur_vert_num;
            auto i = cb->_cur_index_num;

            cb->_pos_buf[v + 0] = {p0, depth};
            cb->_pos_buf[v + 1] = {p1, depth};
            cb->_pos_buf[v + 2] = {p2, depth};
            cb->_pos_buf[v + 3] = {p3, depth};

            // uv 用不到，可以全 0
            cb->_uv_buf[v + 0] = {0, 0};
            cb->_uv_buf[v + 1] = {0, 0};
            cb->_uv_buf[v + 2] = {0, 0};
            cb->_uv_buf[v + 3] = {0, 0};

            cb->_color_buf[v + 0] = color;
            cb->_color_buf[v + 1] = color;
            cb->_color_buf[v + 2] = color;
            cb->_color_buf[v + 3] = color;

            cb->_index_buf[i + 0] = v + 0;
            cb->_index_buf[i + 1] = v + 1;
            cb->_index_buf[i + 2] = v + 2;
            cb->_index_buf[i + 3] = v + 1;
            cb->_index_buf[i + 4] = v + 3;
            cb->_index_buf[i + 5] = v + 2;

            cb->_cur_vert_num += 4;
            cb->_cur_index_num += 6;
        }
        void UIRenderer::DrawBox(Vector2f pos, Vector2f size, f32 thickness, Color color, f32 depth)
        {
            Vector2f p0 = pos;
            Vector2f p1 = {pos.x + size.x, pos.y};
            Vector2f p2 = {pos.x + size.x, pos.y + size.y};
            Vector2f p3 = {pos.x, pos.y + size.y};

            DrawLine(p0, p1, thickness, color, depth);// top
            DrawLine(p1, p2, thickness, color, depth);// right
            DrawLine(p2, p3, thickness, color, depth);// bottom
            DrawLine(p3, p0, thickness, color, depth);// left
        }
        void UIRenderer::AddWidget(Ref<Widget> w)
        {
            if (auto it = std::find_if(_widgets.begin(),_widgets.end(),[&](auto e)->bool {return e.get() == w.get();});it != _widgets.end())
                return;
            _widgets.emplace_back(w);
            _active_widgets.emplace_back(_widgets.back());
        }

        void UIRenderer::RemoveWidget(Widget *canvas)
        {
            std::erase_if(_active_widgets, [&](Ref<Widget> &c)
                          { return c.get() == canvas; });
        }

        void UIRenderer::DeleteWidget(Widget *canvas)
        {
            std::erase_if(_widgets, [&](Ref<Widget> &c)
                          { return c.get() == canvas; });
            RemoveWidget(canvas);
        }

        UIRenderer::DrawerBlock *UIRenderer::GetAvailableBlock(u32 vert_num)
        {
            DrawerBlock *available_block = nullptr;
            auto &frame_block = _drawer_blocks[_frame_index];
            if (frame_block.size() < _cur_widget_index + 1u)
            {
                frame_block.push_back(AL_NEW(DrawerBlock));
                available_block = frame_block.back();
            }
            else
            {
                //for (auto b: frame_block)
                //{
                //    if (b->_cur_vert_num + vert_num <= b->_max_vert_num)
                //    {
                //        available_block = b;
                //        break;
                //    }
                //}
                //if (available_block == nullptr)
                //{
                //    frame_block.push_back(AL_NEW(DrawerBlock));
                //    available_block = frame_block.back();
                //}
                available_block = frame_block[_cur_widget_index];
                AL_ASSERT_MSG(available_block->_cur_vert_num + vert_num <= available_block->_max_vert_num, "UI DrawerBlock index overflow!");
            }
            return available_block;
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
}// namespace Ailu