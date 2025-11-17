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
        const static Matrix4x4f kIdentityMatrix = Matrix4x4f::Identity();
        TextRenderer::TextRenderer()
        {
            TextRenderer::Initialize();
        }
        void TextRenderer::Initialize()
        {
            TIMER_BLOCK("TextRenderer::Init")
            s_default_font = g_pResourceMgr->_default_font.get();
            _bitmap_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_text.alasset"), "DefaultTextMaterial");
            _bitmap_mat->SetTexture("_MainTex", s_default_font->_pages[0]._texture.get());
            _msdf_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_text.alasset"), "DefaultTextMaterial");
            _msdf_mat->SetTexture("_MainTex", s_default_font->_pages[0]._texture.get());
            _msdf_mat->EnableKeyword("_MSDF");
            _default_block = new DrawerBlock(_bitmap_mat);
        }
        TextRenderer::~TextRenderer()
        {
            DESTORY_PTR(_default_block);
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
        void TextRenderer::Render(RenderTexture *target, Render::CommandBuffer *cmd)
        {
            Render(target, cmd, _default_block);
            _default_block->Flush();
        }

        void TextRenderer::AppendText(const String &text, Vector2f pos, Matrix4x4f matrix, f32 font_size, Vector2f scale, Color color, Vector2f padding, Font *font, DrawerBlock *block)
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
                /*
                 0----3
                 |   /|
                 |  / |
                 | /  |
                 1----2
                */
                block->_uv_buf[v_base]     = {uv_rect.x, uv_rect.y};
                block->_uv_buf[v_base + 1] = {uv_rect.x, uv_rect.y + uv_rect.w};
                block->_uv_buf[v_base + 2] = {uv_rect.x + uv_rect.z, uv_rect.y + uv_rect.w};
                block->_uv_buf[v_base + 3] = {uv_rect.x + uv_rect.z, uv_rect.y};
                block->_pos_buf[v_base]     = {pos_rect.xy, 1.0};
                block->_pos_buf[v_base + 1] = {pos_rect.x, pos_rect.y + pos_rect.w, 1.0};
                block->_pos_buf[v_base + 2] = {pos_rect.x + pos_rect.z, pos_rect.y + pos_rect.w, 1.0};
                block->_pos_buf[v_base + 3] = {pos_rect.x + pos_rect.z, pos_rect.y, 1.0};
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
                block->_index_buf[i_base + 2] = v_base + 3;
                block->_index_buf[i_base + 3] = v_base + 1;
                block->_index_buf[i_base + 4] = v_base + 2;
                block->_index_buf[i_base + 5] = v_base + 3;

                UIRenderer::Get()->AppendNode(block, 4u, 6u, font->_is_msdf? _msdf_mat.get() : _bitmap_mat.get(), font->_pages[font->GetChar(text[0])._page]._texture.get());
            }
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
        void TextRenderer::Render(Render::CommandBuffer *cmd)
        {
            if (_default_block->_nodes.empty())
                return;
            _default_block->SubmitVertexData();
            Render::CBufferPerObjectData per_obj_data;
            per_obj_data._MatrixWorld = BuildIdentityMatrix();
            memcpy(_default_block->_obj_cb->GetData(), &per_obj_data, Render::RenderConstants::kPerObjectDataSize);
            for (const auto &node: _default_block->_nodes)
            {
                cmd->DrawIndexed(_default_block->_vbuf, _default_block->_ibuf, _default_block->_obj_cb, node._mat, 0u, node._index_offset, node._index_num);
            }
            _default_block->Flush();
        }
        Vector2f TextRenderer::CalculateTextSize(const String &text, f32 font_size, Font *font, Vector2f scale)
        {
            if (text.empty())
                return Vector2f::kZero;
            font = font ? font : s_default_font;
            Vector2f pos = Vector2f::kZero, padding = Vector2f::kZero;
            auto glyphs = LayoutText(text, pos, font_size, scale, padding, font);
            Vector2f size = Vector2f::kZero;
            auto &b = glyphs.back();
            return {b._pos.x + b._size.x, font->_line_height * scale.y * font_size};
        }
    }// namespace UI
}// namespace Ailu
