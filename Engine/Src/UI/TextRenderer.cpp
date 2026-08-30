//
// Created by 22292 on 2024/10/24.
//
#include "UI/TextRenderer.h"
#include "UI/UIRenderer.h"
#include "Render/CommandBuffer.h"
#include "Render/Gizmo.h"
#include "Render/Material.h"
#include <Framework/Common/Profiler.h>
#include <Framework/Common/ResourceMgr.h>
#include <Framework/Common/Allocator.hpp>
#include <limits>

namespace Ailu
{
    namespace UI
    {
        const static Matrix4x4f kIdentityMatrix = Matrix4x4f::Identity();
        TextRenderer::TextRenderer()
        {
            TextRenderer::Initialize();
        }
        void TextRenderer::Initialize()
        {
            TIMER_BLOCK("TextRenderer::Init")
            s_default_font = ResourceMgr::Get()._default_font.get();
            _bitmap_mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_text.alasset"), "DefaultTextMaterial");
            _bitmap_mat->SetTexture("_MainTex", s_default_font->_pages[0]._texture.get());
            _msdf_mat = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_text.alasset"), "DefaultTextMaterial");
            _msdf_mat->SetTexture("_MainTex", s_default_font->_pages[0]._texture.get());
            _msdf_mat->EnableKeyword("_MSDF");
            _default_block = AL_NEW_TAG(EMemoryTag::kUi, DrawerBlock, _bitmap_mat);
        }
        TextRenderer::~TextRenderer()
        {
            AL_DELETE(_default_block);
        }
        void TextRenderer::DrawText(const String &text, Vector2f pos, f32 font_size, Vector2f scale, Color color, Font *font)
        {
            font = font ? font : s_default_font;
            AppendText(text, pos, kIdentityMatrix,font_size, scale, color, Vector2f::kZero, font, _default_block);
        }
        void TextRenderer::DrawText(const String &text, Vector2f pos, f32 font_size, Vector2f scale, Color color, Font *font, DrawerBlock *block)
        {
            font = font ? font : s_default_font;
            AppendText(text, pos, kIdentityMatrix, font_size, scale, color, Vector2f::kZero, font, block);
        }
        void TextRenderer::DrawText(const String &text, Vector2f pos, f32 font_size, Vector2f scale, Color color, Matrix4x4f matrix, Font *font, DrawerBlock *block)
        {
            font = font ? font : s_default_font;
            AppendText(text, pos, matrix, font_size, scale, color, Vector2f::kZero, font, block);
        }
        void TextRenderer::DrawTextLayout(const Render::TextLayoutResult &layout, Vector2f pos, f32 font_size, Vector2f scale, Color color, Font *font, DrawerBlock *block)
        {
            font = font ? font : s_default_font;
            AppendTextLayout(layout, pos, kIdentityMatrix, color, font, block);
        }
        void TextRenderer::DrawTextLayout(const Render::TextLayoutResult &layout, Vector2f pos, f32 font_size, Vector2f scale, Color color, Matrix4x4f matrix, Font *font, DrawerBlock *block)
        {
            font = font ? font : s_default_font;
            AppendTextLayout(layout, pos, matrix, color, font, block);
        }
        void TextRenderer::Render(RenderTexture *target, Render::CommandBuffer *cmd)
        {
            Render(target, cmd, _default_block);
            _default_block->ResetBuildData();
        }

        void TextRenderer::AppendText(const String &text, Vector2f pos, Matrix4x4f matrix, f32 font_size, Vector2f scale, Color color, Vector2f padding, Font *font, DrawerBlock *block)
        {
            if (text.empty())
                return;
            auto layout = BuildLayout(text, pos, font_size, font, scale);
            AppendTextLayout(layout, Vector2f::kZero, matrix, color, font, block);
        }

        void TextRenderer::AppendTextLayout(const Render::TextLayoutResult &layout, Vector2f pos, Matrix4x4f matrix, Color color, Font *font, DrawerBlock *block)
        {
            if (layout._glyphs.empty())
                return;
            for (auto &g: layout._glyphs)
            {
                if (!block->CanAppend(4, 6)) break;
                Vector4f pos_rect = {g._pos.x + pos.x, g._pos.y + pos.y, g._size.x, g._size.y};
                Vector4f uv_rect = {g._uv.x, g._uv.y, g._uv_size.x, g._uv_size.y};
                u32 v_base = block->CurrentVertNum();
                u32 i_base = block->CurrentIndexNum();
                /*
                 0----1
                 |   /|
                 |  / |
                 | /  |
                 2----3
                */
                block->_uv_buf[v_base]     = {uv_rect.x, uv_rect.y};
                block->_uv_buf[v_base + 1] = {uv_rect.x + uv_rect.z, uv_rect.y};
                block->_uv_buf[v_base + 2] = {uv_rect.x, uv_rect.y + uv_rect.w};
                block->_uv_buf[v_base + 3] = {uv_rect.x + uv_rect.z, uv_rect.y + uv_rect.w};
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
                block->_rect_buf[v_base] = pos_rect;
                block->_rect_buf[v_base + 1] = pos_rect;
                block->_rect_buf[v_base + 2] = pos_rect;
                block->_rect_buf[v_base + 3] = pos_rect;
                block->_corner_radius_buf[v_base] = Vector4f::kZero;
                block->_corner_radius_buf[v_base + 1] = Vector4f::kZero;
                block->_corner_radius_buf[v_base + 2] = Vector4f::kZero;
                block->_corner_radius_buf[v_base + 3] = Vector4f::kZero;
                block->_border_thickness_buf[v_base] = Vector4f::kZero;
                block->_border_thickness_buf[v_base + 1] = Vector4f::kZero;
                block->_border_thickness_buf[v_base + 2] = Vector4f::kZero;
                block->_border_thickness_buf[v_base + 3] = Vector4f::kZero;
                block->_index_buf[i_base] = v_base;
                block->_index_buf[i_base + 1] = v_base + 1;
                block->_index_buf[i_base + 2] = v_base + 2;
                block->_index_buf[i_base + 3] = v_base + 1;
                block->_index_buf[i_base + 4] = v_base + 3;
                block->_index_buf[i_base + 5] = v_base + 2;

                UIRenderer::Get()->AppendNode(block,
                                              4u,
                                              6u,
                                              font->_is_msdf ? _msdf_mat.get() : _bitmap_mat.get(),
                                              font->_pages[g._page]._texture.get(),
                                              font->_is_msdf ? font->_msdf_distance_range : 0.0f);
            }
        }

        void TextRenderer::Render(RenderTexture *target, Render::CommandBuffer *cmd, DrawerBlock *b)
        {
            if (b->_nodes.empty())
                return;
            if (auto renderer = UIRenderer::Get(); renderer != nullptr)
                renderer->MutableStats()._ui_uploaded_bytes += b->SubmitVertexData();
            else
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
                cmd->DrawIndexed(b->_vbuf.get(), b->_ibuf.get(), b->_obj_cb.get(), node._mat, 0u, node._index_offset, node._index_num);
            }
        }
        void TextRenderer::Render(Render::CommandBuffer *cmd)
        {
            if (_default_block->_nodes.empty())
                return;
            if (auto renderer = UIRenderer::Get(); renderer != nullptr)
                renderer->MutableStats()._ui_uploaded_bytes += _default_block->SubmitVertexData();
            else
                _default_block->SubmitVertexData();
            Render::CBufferPerObjectData per_obj_data;
            per_obj_data._MatrixWorld = BuildIdentityMatrix();
            memcpy(_default_block->_obj_cb->GetData(), &per_obj_data, Render::RenderConstants::kPerObjectDataSize);
            for (const auto &node: _default_block->_nodes)
            {
                cmd->DrawIndexed(_default_block->_vbuf.get(), _default_block->_ibuf.get(), _default_block->_obj_cb.get(), node._mat, 0u, node._index_offset, node._index_num);
            }
            _default_block->ResetBuildData();
        }
        Render::TextLayoutResult TextRenderer::BuildLayout(const String &text, Vector2f pos, f32 font_size, Font *font, Vector2f scale)
        {
            if (text.empty())
                return {};

            font = font ? font : s_default_font;
            if (auto renderer = UIRenderer::Get(); renderer != nullptr)
                ++renderer->MutableStats()._ui_text_layout_count;
            return LayoutText(text, pos, font_size, scale, Vector2f::kZero, font);
        }

        Vector2f TextRenderer::CalculateTextSize(const String &text, f32 font_size, Font *font, Vector2f scale)
        {
            if (text.empty())
                return Vector2f::kZero;

            return BuildLayout(text, Vector2f::kZero, font_size, font, scale)._size;
        }

        Vector4f TextRenderer::CalculateTextVisualBounds(const String &text, f32 font_size, Font *font, Vector2f scale)
        {
            if (text.empty())
                return Vector4f::kZero;

            const auto layout = BuildLayout(text, Vector2f::kZero, font_size, font, scale);
            return CalculateTextVisualBounds(layout);
        }

        Vector4f TextRenderer::CalculateTextVisualBounds(const Render::TextLayoutResult &layout)
        {
            if (layout._glyphs.empty())
                return Vector4f(0.0f, 0.0f, layout._size.x, layout._size.y);

            f32 min_x = std::numeric_limits<f32>::max();
            f32 min_y = std::numeric_limits<f32>::max();
            f32 max_x = std::numeric_limits<f32>::lowest();
            f32 max_y = std::numeric_limits<f32>::lowest();
            for (const auto &glyph: layout._glyphs)
            {
                min_x = std::min(min_x, glyph._pos.x);
                min_y = std::min(min_y, glyph._pos.y);
                max_x = std::max(max_x, glyph._pos.x + glyph._size.x);
                max_y = std::max(max_y, glyph._pos.y + glyph._size.y);
            }
            return Vector4f(min_x, min_y, max_x - min_x, max_y - min_y);
        }
    }// namespace UI
}// namespace Ailu
