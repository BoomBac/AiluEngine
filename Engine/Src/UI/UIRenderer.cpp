//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/UIRenderer.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include "UI/TextRenderer.h"
#include "UI/Widget.h"
#include "UI/UIFramework.h"
#include "UI/DragDrop.h"
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
            _default_material = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_ui.alasset"), "DefaultUIMaterial");
            _default_material->SetTexture("_MainTex", Render::Texture::s_p_default_white);
            for (auto &frame_blocks: _drawer_blocks)
            {
                frame_blocks.push_back(AL_NEW(DrawerBlock, _default_material,9600u));
            }
            _text_block = AL_NEW(DrawerBlock,MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_text.alasset"), "DefaultTextMaterial"));
            _text_renderer = MakeScope<TextRenderer>();
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
            AL_DELETE(_text_block);
        }

        void UIRenderer::Render(CommandBuffer *cmd)
        {
            DrawDebugPannel();
            const f32 dt = TimeMgr::s_delta_time;
            auto& widgets = UI::UIManager::Get()->_widgets;
            for (auto it = widgets.begin(); it != widgets.end(); it++)
            {
                auto canvas = it->get();
                if (canvas->_visibility != EVisibility::kVisible)
                    continue;
                canvas->PreUpdate(dt);
            }
            for (auto it = widgets.begin(); it != widgets.end(); it++)
            {
                auto canvas = it->get();
                if (canvas->_visibility != EVisibility::kVisible)
                    continue;
                canvas->Update(dt);
            }
            DragDropManager::Get().Update();
            _cur_widget_index = 1u;
            for (auto it = widgets.begin();it != widgets.end(); it++)
            {
                auto canvas = it->get();
                if (canvas->_visibility != EVisibility::kVisible)
                    continue;
                canvas->Render(*this);
                auto b = _drawer_blocks[_frame_index][_cur_widget_index];
                if (b->_nodes.empty())
                    continue;

                auto [color, depth] = canvas->GetOutput();
                SubmitBlock(b,cmd,color,depth);
                ++_cur_widget_index;
            }
            //绘制全局gui
            SubmitBlock(_drawer_blocks[_frame_index][0u],cmd,RenderTexture::s_backbuffer);
            _cur_widget_index = 0u;
            _drawer_blocks[_frame_index][0u]->Flush();
            //暂时所有文本都渲染到后备缓冲区
            //TextRenderer::Get()->Render(RenderTexture::s_backbuffer, cmd, _text_block);
            //_text_block->Clear();
        }

        void UIRenderer::DrawQuad(Vector4f rect, Color color, f32 depth)
        {
            DrawQuad(rect, kIdentityMatrix, color, depth);
        }

        void UIRenderer::DrawQuad(Vector4f rect,Matrix4x4f matrix, Color color, f32 depth)
        {
            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            u32 cur_vert_num = cb->CurrentVertNum(), cur_index_num = cb->CurrentIndexNum();
            cb->_pos_buf[cur_vert_num] = {rect.xy, depth};
            cb->_pos_buf[cur_vert_num + 1] = {rect.x + rect.z, rect.y, depth};
            cb->_pos_buf[cur_vert_num + 2] = {rect.x, rect.y + rect.w, depth};
            cb->_pos_buf[cur_vert_num + 3] = {rect.x + rect.z, rect.y + rect.w, depth};
            TransformCoord(cb->_pos_buf[cur_vert_num], matrix);
            TransformCoord(cb->_pos_buf[cur_vert_num + 1], matrix);
            TransformCoord(cb->_pos_buf[cur_vert_num + 2], matrix);
            TransformCoord(cb->_pos_buf[cur_vert_num + 3], matrix);
            cb->_uv_buf[cur_vert_num] = {0.f, 0.f};
            cb->_uv_buf[cur_vert_num + 1] = {1.f, 0.f};
            cb->_uv_buf[cur_vert_num + 2] = {0.f, 1.f};
            cb->_uv_buf[cur_vert_num + 3] = {1.f, 1.f};
            cb->_color_buf[cur_vert_num] = color;
            cb->_color_buf[cur_vert_num + 1] = color;
            cb->_color_buf[cur_vert_num + 2] = color;
            cb->_color_buf[cur_vert_num + 3] = color;
            cb->_index_buf[cur_index_num] = cur_vert_num + 0u;
            cb->_index_buf[cur_index_num + 1] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 2] = cur_vert_num + 2u;
            cb->_index_buf[cur_index_num + 3] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 4] = cur_vert_num + 3u;
            cb->_index_buf[cur_index_num + 5] = cur_vert_num + 2u;
            AppendNode(cb, 4u, 6u, _default_material.get());
        }

        void UIRenderer::DrawText(const String &text, Vector2f pos, f32 font_size, Color color,Vector2f scale, Render::Font *font)
        {
            _text_renderer->DrawText(text, pos, font_size, scale, color, font, GetAvailableBlock(4u, 6u));
        }

        void UIRenderer::DrawText(const String &text, Vector2f pos, Matrix4x4f matrix, f32 font_size, Color color, Vector2f scale, Render::Font *font)
        {
            _text_renderer->DrawText(text, pos, font_size, scale, color, matrix, font, GetAvailableBlock(4u, 6u));
        }

        void UIRenderer::DrawImage(Render::Texture *texture, Vector4f rect, const ImageDrawOptions &opts)
        {
            if (!texture)
                return;
            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            u32 cur_vert_num = cb->CurrentVertNum(), cur_index_num = cb->CurrentIndexNum();
            cb->_pos_buf[cur_vert_num] = {rect.xy, opts._depth};
            cb->_pos_buf[cur_vert_num + 1] = {rect.x + rect.z, rect.y, opts._depth};
            cb->_pos_buf[cur_vert_num + 2] = {rect.x, rect.y + rect.w, opts._depth};
            cb->_pos_buf[cur_vert_num + 3] = {rect.x + rect.z, rect.y + rect.w, opts._depth};
            TransformCoord(cb->_pos_buf[cur_vert_num], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 1], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 2], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 3], opts._transform);
            cb->_uv_buf[cur_vert_num] = {0.f, 0.f};
            cb->_uv_buf[cur_vert_num + 1] = {1.f, 0.f};
            cb->_uv_buf[cur_vert_num + 2] = {0.f, 1.f};
            cb->_uv_buf[cur_vert_num + 3] = {1.f, 1.f};
            cb->_color_buf[cur_vert_num]     = opts._tint;
            cb->_color_buf[cur_vert_num + 1] = opts._tint;
            cb->_color_buf[cur_vert_num + 2] = opts._tint;
            cb->_color_buf[cur_vert_num + 3] = opts._tint;
            cb->_index_buf[cur_index_num] = cur_vert_num + 0u;
            cb->_index_buf[cur_index_num + 1] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 2] = cur_vert_num + 2u;
            cb->_index_buf[cur_index_num + 3] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 4] = cur_vert_num + 3u;
            cb->_index_buf[cur_index_num + 5] = cur_vert_num + 2u;
            AppendNode(cb, 4u, 6u, _default_material.get(),texture);
        }

        void UIRenderer::DrawLine(Vector2f a, Vector2f b, f32 thickness, Color color, f32 depth)
        {
            DrawLine(a, b, kIdentityMatrix, thickness, color, depth);
        }
        void UIRenderer::DrawLine(Vector2f a, Vector2f b, Matrix4x4f matrix, f32 thickness, Color color, f32 depth)
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

            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            auto v = cb->CurrentVertNum();
            auto i = cb->CurrentIndexNum();

            cb->_pos_buf[v + 0] = {p0, depth};
            cb->_pos_buf[v + 1] = {p1, depth};
            cb->_pos_buf[v + 2] = {p2, depth};
            cb->_pos_buf[v + 3] = {p3, depth};
            TransformCoord(cb->_pos_buf[v + 0], matrix);
            TransformCoord(cb->_pos_buf[v + 1], matrix);
            TransformCoord(cb->_pos_buf[v + 2], matrix);
            TransformCoord(cb->_pos_buf[v + 3], matrix);

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
            AppendNode(cb, 4u, 6u, _default_material.get());
        }
        void UIRenderer::DrawBox(Vector2f pos, Vector2f size, f32 thickness, Color color, f32 depth)
        {
            DrawBox(pos, size, kIdentityMatrix, thickness, color, depth);
        }

        void UIRenderer::DrawBox(Vector2f pos, Vector2f size, Matrix4x4f matrix, f32 thickness, Color color, f32 depth)
        {
            Vector2f p0 = pos;
            Vector2f p1 = {pos.x + size.x, pos.y};
            Vector2f p2 = {pos.x + size.x, pos.y + size.y};
            Vector2f p3 = {pos.x, pos.y + size.y};

            DrawLine(p0, p1, matrix,thickness, color, depth);// top
            DrawLine(p1, p2, matrix,thickness, color, depth);// right
            DrawLine(p2, p3, matrix,thickness, color, depth);// bottom
            DrawLine(p3, p0, matrix,thickness, color, depth);// left
        }

        void UIRenderer::PushScissor(Vector4f scissor)
        {
            Clamp(scissor.x, 0.f, 65535.f);
            Clamp(scissor.y, 0.f, 65535.f);
            _scissor_stack.push_back(Rect((u16) scissor.x, (u16) scissor.y, (u16) (scissor.x + scissor.z), (u16) (scissor.y + scissor.w)));
        }

        void UIRenderer::PopScissor()
        {
            _scissor_stack.erase(_scissor_stack.end()-1);
        }

        Vector2f UIRenderer::CalculateTextSize(const String &text,u16 font_size, Vector2f scale, Render::Font *font)
        {
            return _text_renderer->CalculateTextSize(text, font_size, font, scale);
        }

        void UIRenderer::AppendNode(DrawerBlock *block, u32 vert_num, u32 index_num, Render::Material *mat, Render::Texture *tex)
        {
            if (!_scissor_stack.empty())
                block->AppendNode(vert_num, index_num, mat, tex,_scissor_stack.back());
            else
                block->AppendNode(vert_num, index_num, mat, tex);
        }

        DrawerBlock *UIRenderer::GetAvailableBlock(u32 vert_num, u32 index_num)
        {
            DrawerBlock *available_block = nullptr;
            auto &frame_block = _drawer_blocks[_frame_index];
            if (frame_block.size() < _cur_widget_index + 1u)
            {
                frame_block.push_back(AL_NEW(DrawerBlock, _default_material,8092u));
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
                AL_ASSERT_MSG(available_block->CanAppend(vert_num,index_num), "UI DrawerBlock index overflow!");
            }
            return available_block;
        }
        void UIRenderer::SubmitBlock(DrawerBlock *b, CommandBuffer *cmd,RenderTexture* color,RenderTexture* depth)
        {
            f32 w = (f32) color->Width();
            f32 h = (f32) color->Height();
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
            b->SubmitVertexData();
            Rect full_rect(0u, 0u, (u16) w, (u16) h);
            Rect prev_scissor = full_rect;
            for (const auto& node: b->_nodes)
            {
                node._mat->SetTexture("_MainTex", node._main_tex? node._main_tex : Texture::s_p_default_white);
                if (node._is_custom_scissor)
                {
                    if (prev_scissor != node._scissor)
                        cmd->SetScissorRect(node._scissor);
                }
                else
                {
                    if (prev_scissor != full_rect)
                        cmd->SetScissorRect(full_rect);
                }
                cmd->DrawIndexed(b->_vbuf, b->_ibuf, _obj_cb.get(), node._mat,0u,node._index_offset,node._index_num);
                prev_scissor = node._is_custom_scissor ? node._scissor : full_rect;
            }
            b->Flush();
        }

        void UIRenderer::DrawDebugPannel()
        {
            Vector2f pen = {10.f, 10.f};
            f32 font_size = 14.f;
            if (auto hover = UIManager::Get()->_hover_target)
            {
                const f32 line_height = _text_renderer->GetDefaultFont()->_line_height * font_size;
                auto abs_rect = hover->GetArrangeRect();
                DrawText(std::format("Name: {},type: {}", hover->Name(), hover->GetType()->Name()), pen, font_size);
                pen.y += line_height;
                DrawText(std::format("Pos: {},Size: {}", Vector2f(abs_rect.xy).ToString(), Vector2f(abs_rect.zw).ToString()), pen, font_size);
                pen.y += line_height;
                DrawText(std::format("Padding: {},Margin: {}", hover->SlotPadding().ToString(), hover->SlotMargin().ToString()),pen,font_size);
                pen.y += line_height;
                DrawText(std::format("SizePolicyH: {},SizePolicyV: {}", StaticEnum<UI::ESizePolicy>()->GetNameByEnum(hover->SlotSizePolicy(true)),
                                     StaticEnum<UI::ESizePolicy>()->GetNameByEnum(hover->SlotSizePolicy(false))), pen, font_size);
                pen.y += line_height;
                DrawText(std::format("AlighH: {},AlighV: {}", StaticEnum<UI::EAlignment>()->GetNameByEnum(hover->SlotAlignmentH()),
                                     StaticEnum<UI::EAlignment>()->GetNameByEnum(hover->SlotAlignmentV())),
                         pen, font_size);
                pen.y += line_height;
                DrawText(std::format("IsFocused: {},IsHover: {},IsPressed: {}", hover->_state._is_focused, hover->_state._is_hovered, hover->_state._is_pressed), pen, font_size);
                DrawBox(hover->GetArrangeRect().xy, hover->GetArrangeRect().zw, 1.0f, Color(1.0f, 0.5f, 0.0f, 1.0f));
            }
        }
    }// namespace UI
}// namespace Ailu