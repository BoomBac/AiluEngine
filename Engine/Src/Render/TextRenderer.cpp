//
// Created by 22292 on 2024/10/24.
//
#include "Render/TextRenderer.h"
#include "Render/CommandBuffer.h"
#include "Render/Gizmo.h"
#include <Framework/Common/ResourceMgr.h>
#include <Framework/Common/Profiler.h>

namespace Ailu
{
    TextRenderer::DrawerBlock::DrawerBlock(u32 char_num)
    {
        Vector<VertexBufferLayoutDesc> desc_list;
        desc_list.emplace_back("POSITION", EShaderDateType::kFloat3, 0);
        desc_list.emplace_back("TEXCOORD", EShaderDateType::kFloat2, 1);
        desc_list.emplace_back("COLOR", EShaderDateType::kFloat4, 2);
        _vbuf = IVertexBuffer::Create(desc_list, "text_vbuf");
        _ibuf = IIndexBuffer::Create(nullptr, kMaxCharacters * 6, "text_ibuf", true);
        _obj_cb = IConstantBuffer::Create(RenderConstants::kPerObjectDataSize);
        _vbuf->SetStream(nullptr, kMaxCharacters * sizeof(Vector3f), 0, true);
        _vbuf->SetStream(nullptr, kMaxCharacters * sizeof(Vector2f), 1, true);
        _vbuf->SetStream(nullptr, kMaxCharacters * sizeof(Vector4f), 2, true);
        //_ibuf->SetData((u8*)indices,6);
        _pos_stream.resize(kMaxCharacters * 4);
        _uv_stream.resize(kMaxCharacters * 4);
        _color_stream.resize(kMaxCharacters * 4);
        _indices.resize(kMaxCharacters * 6);
        _default_mat = MakeRef<Material>(g_pResourceMgr->Get<Shader>(L"Shaders/default_text.alasset"), "DefaultTextMaterial");
        _default_mat->SetTexture("_MainTex", Texture::s_p_default_white);
    }
    TextRenderer::DrawerBlock::~DrawerBlock()
    {
//        delete _vbuf;
//        delete _ibuf;
//        delete _obj_cb;
        DESTORY_PTR(_vbuf);
        DESTORY_PTR(_ibuf);
        DESTORY_PTR(_obj_cb);
    }
    void TextRenderer::DrawerBlock::AppendText(Font *font, const String &text, Vector2f pos, u16 font_size,Vector2f scale, Vector2f padding, Color color)
    {
        if (text.empty())
            return;
        scale *= (f32)font_size / (f32)font->_size;
        f32 x = pos.x, y = pos.y;
        f32 h_padding = (font->_left_padding + font->_right_padding) * padding.x;
        f32 v_padding = (font->_top_padding + font->_bottom_padding) * padding.y;
        char last_char = -1;
        f32 y_adjust = 0.0f;
        for (u32 i = 0; i < text.size(); i++)
        {
            char &c = const_cast<char &>(text[i]);
            auto &char_info = font->GetChar(c);
            if (c == '\0')
                break;
            if (c == '\n')
            {
                x = pos.x;
                y -= (font->_line_height + v_padding) * scale.y;
                last_char = -1;
                continue;
            }
            if (c == ' ')
            {
                x += (font->_left_padding + h_padding) * scale.x;
                last_char = c;
                continue;
            }
            if (_characters_count > kMaxCharacters)
            {
                LOG_WARNING("TextRenderer::DrawText: Too many characters, max is %d", kMaxCharacters);
                break;
            }
            f32 kerning = 0.f;
            if (i > 0)
                kerning = font->GetKerning(last_char, c);
            f32 w = font->_tex_width, h = font->_tex_height;
            u32 v_base = _characters_count * 4, i_base = _characters_count * 6;
            Vector4f uv_rect = Vector4f(char_info._u, char_info._v, char_info._twidth, char_info._theight);
            Vector4f pos_rect = {x + (char_info._xoffset + kerning) * scale.x, y - char_info._yoffset * scale.y, char_info._width, char_info._height};
            if (i == 0)
                y_adjust = pos.y - pos_rect.y;
            pos_rect.y += y_adjust;
            pos_rect.z *= scale.x;
            pos_rect.w *= scale.y;
            _uv_stream[v_base] = uv_rect.xy;
            _uv_stream[v_base + 1] = Vector2f(uv_rect.x + uv_rect.z, uv_rect.y);
            _uv_stream[v_base + 2] = Vector2f(uv_rect.x, uv_rect.y + uv_rect.w);
            _uv_stream[v_base + 3] = Vector2f(uv_rect.x + uv_rect.z, uv_rect.y + uv_rect.w);
            _pos_stream[v_base] = {pos_rect.xy, 1.0};
            _pos_stream[v_base + 1] = {pos_rect.x + pos_rect.z, pos_rect.y, 1.0};
            _pos_stream[v_base + 2] = {pos_rect.x, pos_rect.y - pos_rect.w, 1.0};
            _pos_stream[v_base + 3] = {pos_rect.x + pos_rect.z, pos_rect.y - pos_rect.w, 1.0};
            _color_stream[v_base] = color;
            _color_stream[v_base + 1] = color;
            _color_stream[v_base + 2] = color;
            _color_stream[v_base + 3] = color;
            _indices[i_base] = v_base;
            _indices[i_base + 1] = v_base + 1;
            _indices[i_base + 2] = v_base + 2;
            _indices[i_base + 3] = v_base + 1;
            _indices[i_base + 4] = v_base + 3;
            _indices[i_base + 5] = v_base + 2;
            ++_characters_count;
            x += (char_info._xadvance - h_padding) * scale.x;
            last_char = c;
        }
        _default_mat->SetTexture("_MainTex", font->_pages[font->GetChar(text[0])._page]._texture.get());
    }

    void TextRenderer::DrawerBlock::Render(RenderTexture *target, CommandBuffer *cmd, bool is_debug)
    {
        _vbuf->SetData((u8 *) _pos_stream.data(), _characters_count * sizeof(Vector3f) * 4, 0, 0);
        _vbuf->SetData((u8 *) _uv_stream.data(), _characters_count * sizeof(Vector2f) * 4, 1, 0);
        _vbuf->SetData((u8 *) _color_stream.data(), _characters_count * sizeof(Vector4f) * 4, 2, 0);
        _ibuf->SetData((u8 *) _indices.data(), _characters_count * sizeof(u32) * 6);
        if (is_debug)
        {
            for (u32 i = 0; i < _characters_count; ++i)
            {
                Gizmo::DrawLine(_pos_stream[i * 4].xy, _pos_stream[i * 4 + 1].xy, Colors::kGreen);
                Gizmo::DrawLine(_pos_stream[i * 4 + 1].xy, _pos_stream[i * 4 + 3].xy, Colors::kGreen);
                Gizmo::DrawLine(_pos_stream[i * 4 + 3].xy, _pos_stream[i * 4 + 2].xy, Colors::kGreen);
                Gizmo::DrawLine(_pos_stream[i * 4 + 2].xy, _pos_stream[i * 4].xy, Colors::kGreen);
            }
        }
        f32 w = (f32) target->Width();
        f32 h = (f32) target->Height();
        CBufferPerCameraData cb_per_cam;
        cb_per_cam._MatrixVP = Camera::GetDefaultOrthogonalViewProj(w, h);
        cb_per_cam._ScreenParams = Vector4f(w, h, 1 / w, 1 / h);
        CBufferPerObjectData per_obj_data;
        per_obj_data._MatrixWorld = MatrixTranslation(-w*0.5f,h*0.5f,0);
        memcpy(_obj_cb->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
        cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &cb_per_cam, RenderConstants::kPerCameraDataSize);
        cmd->SetRenderTarget(target);
        cmd->DrawIndexed(_vbuf, _ibuf, _obj_cb, _default_mat.get());
    }

    void TextRenderer::DrawerBlock::ClearBuffer()
    {
        _characters_count = 0;
    }
    TextRenderer::DrawerBlock& TextRenderer::DrawerBlock::operator=(TextRenderer::DrawerBlock &&other) noexcept
    {
        _vbuf = other._vbuf;
        _ibuf = other._ibuf;
        _obj_cb = other._obj_cb;
        _pos_stream = std::move(other._pos_stream);
        _uv_stream = std::move(other._uv_stream);
        _color_stream = std::move(other._color_stream);
        _indices = std::move(other._indices);
        _characters_count = other._characters_count;
        other._characters_count = 0;
        other._vbuf = nullptr;
        other._ibuf = nullptr;
        other._obj_cb = nullptr;
        return *this;
    }
    static TextRenderer *s_pTextRenderer = nullptr;
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
        for (auto &b: _drawer_blocks)
        {
            b[_default_font->_name] = new DrawerBlock();
        }
    }
    TextRenderer::~TextRenderer()
    {
        for (auto & b: _drawer_blocks)
        {
            for (auto& it : b)
                DESTORY_PTR(it.second);
        }
    }
    void TextRenderer::DrawText(const String &text, Vector2f pos, u16 font_size,Vector2f scale, Color color, Font *font)
    {
        s_pTextRenderer->_drawer_blocks[s_pTextRenderer->_current_frame_index][font->_name]->AppendText(font, text, pos, font_size,scale, Vector2f(1.f, 1.f), color);
    }
    void TextRenderer::Render(RenderTexture* target, CommandBuffer *cmd)
    {
        for (auto &it: _drawer_blocks[_current_frame_index])
        {
            it.second->Render(target, cmd, _is_draw_debug_line);
            it.second->ClearBuffer();
        }
    }
    Font *TextRenderer::GetDefaultFont() const
    {
        return _default_font.get();
    }
    void TextRenderer::Render(RenderTexture *target)
    {
        auto cmd = CommandBufferPool::Get("UI");
        {
            PROFILE_BLOCK_GPU(cmd.get(), Text);
            Render(target, cmd.get());
        }
        g_pGfxContext->ExecuteCommandBuffer(cmd);
        CommandBufferPool::Release(cmd);
    }
    Vector2f TextRenderer::CalculateTextSize(const String &text, Font* font,u16 font_size, Vector2f scale)
    {
        if (text.empty())
            return {0.f,0.f};
        Vector2f start,end;
        Vector2f pos = {0.f,0.f};
        Vector2f deserved_size = pos;
        scale *= (f32)font_size / (f32)font->_size;
        f32 x = 0.f, y = 0.f;
        f32 h_padding = (font->_left_padding + font->_right_padding);
        f32 v_padding = (font->_top_padding + font->_bottom_padding);
        char last_char = -1;
        f32 y_adjust = 0.0f;
        for (u32 i = 0; i < text.size(); i++)
        {
            char &c = const_cast<char &>(text[i]);
            auto &char_info = font->GetChar(c);
            if (c == '\0')
                break;
            if (c == '\n')
            {
                x = pos.x;
                y -= (font->_line_height + v_padding) * scale.y;
                last_char = -1;
                continue;
            }
            if (c == ' ')
            {
                x += (font->_left_padding + h_padding) * scale.x;
                last_char = c;
                continue;
            }
            f32 kerning = 0.f;
            if (i > 0)
                kerning = font->GetKerning(last_char, c);
            f32 w = font->_tex_width, h = font->_tex_height;
            Vector4f uv_rect = Vector4f(char_info._u, char_info._v, char_info._twidth, char_info._theight);
            Vector4f pos_rect = {x + (char_info._xoffset + kerning) * scale.x, y - char_info._yoffset * scale.y, char_info._width, char_info._height};
            if (i == 0)
                y_adjust = pos.y - pos_rect.y;
            pos_rect.y += y_adjust;
            pos_rect.z *= scale.x;
            pos_rect.w *= scale.y;
            x += (char_info._xadvance - h_padding) * scale.x;
            last_char = c;
            if (i == 0)
                start = pos_rect.xy;
            if (i == text.size() - 1)
                end = {pos_rect.x + pos_rect.z, pos_rect.y - pos_rect.w};
        }
        deserved_size = end - start;
        deserved_size.x = std::abs(deserved_size.x);
        deserved_size.y = std::abs(deserved_size.y);
        return deserved_size;
    }
}// namespace Ailu
