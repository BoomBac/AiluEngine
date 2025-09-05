//
// Created by 22292 on 2024/10/24.
//
#include "UI/TextRenderer.h"
#include "UI/UIRenderer.h"
#include "Render/CommandBuffer.h"
#include "Render/Gizmo.h"
#include <Framework/Common/Profiler.h>
#include <Framework/Common/ResourceMgr.h>

namespace Ailu
{
    namespace UI
    {
        struct GlyphRenderInfo
        {
            char _c;
            Vector2f _pos;      // 左上角屏幕坐标
            Vector2f _size;     // scaled width/height
            Vector2f _uv;       // u,v起点
            Vector2f _uv_size;  // u,v宽高
            u32 _page;
            f32 _xadvance;    // 光标前进量
        };

        static Vector<GlyphRenderInfo> LayoutText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Vector2f padding, Font *font)
        {
            if (text.empty())
                return {};
            Vector<GlyphRenderInfo> result;
            result.reserve(text.size());
            scale *= (f32) font_size / (f32) font->_size;
            f32 x = pos.x, y = pos.y;
            f32 h_padding = (font->_left_padding + font->_right_padding) * padding.x;
            f32 v_padding = (font->_top_padding + font->_bottom_padding) * padding.y;
            i32 last_char = -1;
            f32 y_adjust = 0.0f;
            for (u32 i = 0; i < text.size(); i++)
            {
                char c = text[i];
                auto &char_info = font->GetChar(c);
                if (c == '\0')
                    continue;
                // 换行
                if (c == '\n')
                {
                    x = pos.x;
                    y -= (font->_line_height + v_padding) * scale.y;
                    last_char = -1;
                    continue;
                }

                // 如果是空格或不可见字符（宽度为0但有advance）
                if (char_info._width == 0 && char_info._height == 0)
                {
                    x += char_info._xadvance * scale.x;
                    last_char = c;
                    continue;
                }

                f32 kerning = (last_char >= 0) ? font->GetKerning(last_char, c) : 0.f;
                Vector4f uv_rect = Vector4f(char_info._u, char_info._v, char_info._twidth, char_info._theight);
                Vector4f pos_rect = {
                        x + (char_info._xoffset + kerning) * scale.x,
                        y + char_info._yoffset * scale.y,
                        char_info._width,
                        char_info._height};
                if (i == 0)
                {
                    auto& cc = font->GetChar('A');
                    y_adjust = (pos.y - (y + cc._yoffset * scale.y));
                }
                pos_rect.y += y_adjust;
                pos_rect.z *= scale.x;
                pos_rect.w *= scale.y;
                GlyphRenderInfo info;
                info._c = c;
                info._pos = pos_rect.xy;
                info._size = pos_rect.zw;
                info._uv = uv_rect.xy;
                info._uv_size = uv_rect.zw;
                info._page = char_info._page;
                info._xadvance = char_info._xadvance * scale.x;
                x += info._xadvance;
                last_char = c;
                result.push_back(info);
            }
            return result;
        }

        static TextRenderer *s_pTextRenderer = nullptr;
        const static Matrix4x4f kIdentityMatrix = Matrix4x4f::Identity();
        void TextRenderer::Init()
        {
            s_pTextRenderer = new TextRenderer();
            s_pTextRenderer->Initialize();
        }
        void TextRenderer::Shutdown()
        {
            DESTORY_PTR(s_pTextRenderer);
        }
        TextRenderer *TextRenderer::Get()
        {
            return s_pTextRenderer;
        }
        TextRenderer::TextRenderer()
        {
        }
        void TextRenderer::Initialize()
        {
            TIMER_BLOCK("TextRenderer::Init")
            _default_font = g_pResourceMgr->_default_font;
            _default_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_text.alasset"), "DefaultTextMaterial");
            _default_block = new DrawerBlock(_default_mat);
        }
        TextRenderer::~TextRenderer()
        {
            DESTORY_PTR(_default_block);
        }
        void TextRenderer::DrawText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Color color, Font *font)
        {
            font = font ? font : _default_font.get();
            AppendText(text, pos, kIdentityMatrix,font_size, scale, color, Vector2f::kZero, font, _default_block);
        }
        void TextRenderer::DrawText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Color color, Font *font, DrawerBlock *block)
        {
            font = font ? font : _default_font.get();
            AppendText(text, pos, kIdentityMatrix, font_size, scale, color, Vector2f::kZero, font, block);
        }
        void TextRenderer::DrawText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Color color, Matrix4x4f matrix, Font *font, DrawerBlock *block)
        {
            font = font ? font : _default_font.get();
            AppendText(text, pos, matrix, font_size, scale, color, Vector2f::kZero, font, block);
        }
        void TextRenderer::Render(RenderTexture *target, Render::CommandBuffer *cmd)
        {
            Render(target, cmd, _default_block);
            _default_block->Flush();
        }
        Font *TextRenderer::GetDefaultFont() const
        {
            return _default_font.get();
        }
        void TextRenderer::AppendText(const String &text, Vector2f pos, Matrix4x4f matrix, u16 font_size, Vector2f scale, Color color, Vector2f padding, Font *font, DrawerBlock *block)
        {
            if (text.empty())
                return;
            auto glyphs = LayoutText(text, pos,font_size, scale, padding, font);
            for (auto &g: glyphs)
            {
                if (!block->CanAppend(4, 6)) break;
                Vector4f pos_rect = {g._pos.x, g._pos.y, g._size.x, g._size.y};
                Vector4f uv_rect = {g._uv.x, g._uv.y, g._uv_size.x, g._uv_size.y};
                u32 v_base = block->CurrentVertNum();
                u32 i_base = block->CurrentIndexNum();
                block->_uv_buf[v_base] = uv_rect.xy;
                block->_uv_buf[v_base + 1] = Vector2f(uv_rect.x + uv_rect.z, uv_rect.y);
                block->_uv_buf[v_base + 2] = Vector2f(uv_rect.x, uv_rect.y + uv_rect.w);
                block->_uv_buf[v_base + 3] = Vector2f(uv_rect.x + uv_rect.z, uv_rect.y + uv_rect.w);
                block->_pos_buf[v_base]     = {pos_rect.xy, 1.0};
                block->_pos_buf[v_base + 1] = {pos_rect.x + pos_rect.z, pos_rect.y, 1.0};
                block->_pos_buf[v_base + 2] = {pos_rect.x, pos_rect.y + pos_rect.w, 1.0};
                block->_pos_buf[v_base + 3] = {pos_rect.x + pos_rect.z, pos_rect.y + pos_rect.w, 1.0};
                TransformCoord(block->_pos_buf[v_base],matrix);
                TransformCoord(block->_pos_buf[v_base + 1], matrix);
                TransformCoord(block->_pos_buf[v_base + 2], matrix);
                TransformCoord(block->_pos_buf[v_base + 3], matrix);
                block->_color_buf[v_base] = color;
                block->_color_buf[v_base + 1] = color;
                block->_color_buf[v_base + 2] = color;
                block->_color_buf[v_base + 3] = color;
                block->_index_buf[i_base] = v_base;
                block->_index_buf[i_base + 1] = v_base + 1;
                block->_index_buf[i_base + 2] = v_base + 2;
                block->_index_buf[i_base + 3] = v_base + 1;
                block->_index_buf[i_base + 4] = v_base + 3;
                block->_index_buf[i_base + 5] = v_base + 2;
                UIRenderer::Get()->AppendNode(block, 4u, 6u, _default_mat.get(), font->_pages[font->GetChar(text[0])._page]._texture.get());
            }
        }
        void TextRenderer::Render(RenderTexture *target)
        {
            auto cmd = Render::CommandBufferPool::Get("Text");
            {
                PROFILE_BLOCK_GPU(cmd.get(), Text);
                Render(target, cmd.get());
            }
            Render::GraphicsContext::Get().ExecuteCommandBuffer(cmd);
            Render::CommandBufferPool::Release(cmd);
        }
        void TextRenderer::Render(RenderTexture *target, Render::CommandBuffer *cmd, DrawerBlock *b)
        {
            if (b->_nodes.empty())
                return;
            b->SubmitVertexData();
            if (_is_draw_debug_line)
            {
                // for (u32 i = 0; i < _characters_count; ++i)
                // {
                //     Gizmo::DrawLine(_pos_stream[i * 4].xy, _pos_stream[i * 4 + 1].xy, Colors::kGreen);
                //     Gizmo::DrawLine(_pos_stream[i * 4 + 1].xy, _pos_stream[i * 4 + 3].xy, Colors::kGreen);
                //     Gizmo::DrawLine(_pos_stream[i * 4 + 3].xy, _pos_stream[i * 4 + 2].xy, Colors::kGreen);
                //     Gizmo::DrawLine(_pos_stream[i * 4 + 2].xy, _pos_stream[i * 4].xy, Colors::kGreen);
                // }
            }
            f32 w = (f32) target->Width();
            f32 h = (f32) target->Height();
            Render::CBufferPerCameraData cb_per_cam;
            Matrix4x4f view, proj;
            BuildViewMatrixLookToLH(view, Vector3f(0.f, 0.f, -50.f), Vector3f::kForward, Vector3f::kUp);
            BuildOrthographicMatrix(proj, 0.0f, w, 0.0f, h, 1.f, 200.f);
            //cb_per_cam._MatrixVP = Camera::GetDefaultOrthogonalViewProj(w, h);
            cb_per_cam._MatrixVP = view * proj;
            cb_per_cam._ScreenParams = Vector4f(1.0f / w, 1.0f / h, w, h);
            Render::CBufferPerObjectData per_obj_data;
            //per_obj_data._MatrixWorld = MatrixTranslation(-w * 0.5f, h * 0.5f, 0);
            per_obj_data._MatrixWorld = BuildIdentityMatrix();
            memcpy(b->_obj_cb->GetData(), &per_obj_data, Render::RenderConstants::kPerObjectDataSize);
            cmd->SetGlobalBuffer(Render::RenderConstants::kCBufNamePerCamera, &cb_per_cam, Render::RenderConstants::kPerCameraDataSize);
            for (const auto &node: b->_nodes)
            {
                cmd->DrawIndexed(b->_vbuf, b->_ibuf, b->_obj_cb, node._mat, 0u, node._index_offset, node._index_num);
            }
        }
        Vector2f TextRenderer::CalculateTextSize(const String &text, u16 font_size, Font *font, Vector2f scale)
        {
            if (text.empty())
                return Vector2f::kZero;
            font = font ? font : Get()->GetDefaultFont();
            Vector2f pos = Vector2f::kZero, padding = Vector2f::kZero;
            auto glyphs = LayoutText(text, pos, font_size, scale, padding, font);
            Vector2f size = Vector2f::kZero;
            f32 max_height = 0.f;
            for (auto& g: glyphs)
            {
                max_height = std::max(max_height, g._size.y);
            }
            auto &b = glyphs.back();
            return {b._pos.x + b._size.x, max_height};
        }
    }// namespace UI
}// namespace Ailu
