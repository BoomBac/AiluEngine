//
// Created by 22292 on 2024/10/24.
//

#include "Render/Font.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu::Render
{
    Ref<Font> Font::Create(const WString &file_path)
    {
        auto parent = PathUtils::Parent(file_path);
        Ref<Font> font = MakeRef<Font>();
        std::stringstream ss;
        String line;
        if (!FileManager::ReadFile(file_path, ss))
        {
            LOG_ERROR(L"Failed to read font file: {}", file_path);
            return nullptr;
        }
        f32 tex_w = 1.0f, tex_h = 1.0f;
        while (std::getline(ss, line))
        {
            std::istringstream iss(line);
            std::string keyword;
            if (line.find("info") == 0)
            {
                // Parse font info
                std::getline(iss, keyword, ' ');// "info"
                while (iss >> keyword)
                {
                    if (keyword.find("face=") == 0)
                    {
                        size_t first_quote = keyword.find('"');
                        size_t second_quote = keyword.find('"', first_quote + 1);
                        font->_name = ToWChar(keyword.substr(6, second_quote - first_quote - 1));
                    }
                    else if (keyword.find("size=") == 0)
                    {
                        font->_size = std::stoi(keyword.substr(5));
                    }
                    else if (keyword.find("lineHeight=") == 0)
                    {
                        font->_line_height = std::stof(keyword.substr(11));
                    }
                    else if (keyword.find("base=") == 0)
                    {
                        font->_base_height = std::stof(keyword.substr(5));
                    }
                    else if (keyword.find("bold=") == 0)
                    {
                        font->_is_bold = std::stoi(keyword.substr(5));
                    }
                    else if (keyword.find("italic=") == 0)
                    {
                        font->_is_italic = std::stoi(keyword.substr(7));
                    }
                    else if (keyword.find("padding") == 0)
                    {
                        String padding_str = keyword.substr(8);
                        Vector4f padding;
                        padding.FromString(padding_str);
                        font->_top_padding = padding.x;
                        font->_right_padding = padding.y;
                        font->_bottom_padding = padding.z;
                        font->_left_padding = padding.w;
                    }
                }
            }
            else if (line.find("common") == 0)
            {
                // Parse common settings
                std::getline(iss, keyword, ' ');// "common"
                while (iss >> keyword)
                {
                    if (keyword.find("scaleW=") == 0)
                    {
                        font->_tex_width = std::stoi(keyword.substr(7));
                        tex_w = (f32) font->_tex_width;
                    }
                    else if (keyword.find("scaleH=") == 0)
                    {
                        font->_tex_height = std::stoi(keyword.substr(7));
                        tex_h = (f32) font->_tex_height;
                    }
                    else if (keyword.find("pages=") == 0)
                    {
                        font->_page_num = std::stoi(keyword.substr(6));
                    }
                    else if (keyword.find("packed=") == 0)
                    {
                        font->_is_packed = std::stoi(keyword.substr(7));
                    }
                    else if (keyword.find("lineHeight=") == 0)
                    {
                        font->_line_height = std::stof(keyword.substr(11));
                    }
                    else if (keyword.find("base=") == 0)
                    {
                        font->_base_height = std::stof(keyword.substr(5));
                    }
                }
            }
            else if (line.find("page") == 0)
            {
                // Parse page info
                std::getline(iss, keyword, ' ');// "page"
                Page p;
                while (iss >> keyword)
                {
                    if (keyword.find("id=") == 0)
                    {
                        p._id = std::stoi(keyword.substr(3));
                    }
                    else if (keyword.find("file=") == 0)
                    {
                        size_t first_quote = keyword.find('"');
                        size_t second_quote = keyword.find('"', first_quote + 1);
                        p._file = parent + ToWChar(keyword.substr(6, second_quote - first_quote - 1));
                    }
                }
                font->_pages.push_back(p);
            }
            else if (line.find("char id") == 0)
            {
                // Parse character info
                FontChar fontChar;
                std::getline(iss, keyword, ' ');// "char"
                while (iss >> keyword)
                {
                    if (keyword.find("id=") == 0)
                    {
                        fontChar._id = std::stoi(keyword.substr(3));
                    }
                    else if (keyword.find("x=") == 0)
                    {
                        fontChar._u = std::stof(keyword.substr(2)) / tex_w;
                    }
                    else if (keyword.find("y=") == 0)
                    {
                        fontChar._v = std::stof(keyword.substr(2)) / tex_h;
                    }
                    else if (keyword.find("width=") == 0)
                    {
                        fontChar._width = std::stof(keyword.substr(6));
                        fontChar._twidth = fontChar._width / tex_w;
                    }
                    else if (keyword.find("height=") == 0)
                    {
                        fontChar._height = std::stof(keyword.substr(7));
                        fontChar._theight = fontChar._height / tex_h;
                    }
                    else if (keyword.find("xoffset=") == 0)
                    {
                        fontChar._xoffset = std::stof(keyword.substr(8));
                    }
                    else if (keyword.find("yoffset=") == 0)
                    {
                        fontChar._yoffset = std::stof(keyword.substr(8));
                    }
                    else if (keyword.find("xadvance=") == 0)
                    {
                        fontChar._xadvance = std::stof(keyword.substr(9));
                    }
                    else if (keyword.find("page=") == 0)
                    {
                        fontChar._page = std::stoi(keyword.substr(5));
                    }
                }
                font->_char_list[(char) fontChar._id] = fontChar;
            }
            else if (line.find("kerning first") == 0)
            {
                FontKerning fontKerning;
                std::getline(iss, keyword, ' ');
                while (iss >> keyword)
                {
                    if (keyword.find("first=") == 0)
                    {
                        fontKerning._first = std::stoi(keyword.substr(6));
                    }
                    else if (keyword.find("second=") == 0)
                    {
                        fontKerning._second = std::stoi(keyword.substr(7));
                    }
                    else if (keyword.find("amount=") == 0)
                    {
                        fontKerning._amount = std::stof(keyword.substr(7));
                    }
                }
                if (font->_kerning_list.find((char) fontKerning._first) == font->_kerning_list.end())
                {
                    font->_kerning_list[(char) fontKerning._first] = {};
                }
                else
                {
                    font->_kerning_list[(char) fontKerning._first][(char) fontKerning._second] = fontKerning._amount;
                }
            }
        }
        return font;
    }
    Vector<GlyphRenderInfo> LayoutText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Vector2f padding, Font *font)
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
                auto &cc = font->GetChar('A');
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
}// namespace Ailu::Render