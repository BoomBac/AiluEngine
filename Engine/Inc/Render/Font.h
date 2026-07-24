//
// Created by 22292 on 2024/10/24.
//

#ifndef AILU_FONT_H
#define AILU_FONT_H
#include "GlobalMarco.h"
#include "Texture.h"
namespace Ailu::Render
{

    struct FontChar
    {
        u16 _id;
        f32 _u;
        f32 _v;
        f32 _twidth; // width in textute
        f32 _theight;
        f32 _width; //with in screen cocrds
        f32 _height;
        //should be normalized based on font size
        f32 _xoffset; //offset from current cursor pos to left side of ch
        f32 _yoffset; //offset from top of line to top of ch
        f32 _xadvance;
        u16 _page;
    };
    struct FontKerning
    {
        u16 _first;
        u16 _second;
        f32 _amount;
    };
    struct Font
    {
        struct Page
        {
            u16 _id;
            WString _file;
            Ref<Texture> _texture;
        };
        WString _name;
        WString _file_path;
        u16 _size; //font size
        f32 _line_height;// should be normalize
        f32 _base_height;// should be normalize
        u16 _tex_width;
        u16 _tex_height;
        u16 _page_num;
        //Vector<FontChar> _char_list;
        Map<char,Map<char,f32>> _kerning_list;
        Map<char,FontChar> _char_list;
        Vector<Page> _pages;
        f32 _left_padding;
        f32 _right_padding;
        f32 _top_padding;
        f32 _bottom_padding;
        f32 _ascent;
        f32 _descent;
        f32 _msdf_distance_range = 0.0f;
        bool _is_bold;
        bool _is_italic;
        bool _is_packed;
        bool _is_msdf = false;
        mutable FontChar _space_char{}; // 用于GetChar中对空白字符的兜底返回
        [[nodiscard]] f32 GetKerning(char first, char second) const
        {
            auto it = _kerning_list.find(first);
            if (it != _kerning_list.end())
            {
                auto it2 = it->second.find(second);
                if (it2 != it->second.end())
                {
                    return it2->second;
                }
            }
            return 0.0f;
        }
        [[nodiscard]] const FontChar& GetChar(char id) const
        {
            // 空白字符：如果不在字体表中，返回零尺寸glyph，仅推进光标
            if (id == ' ' || id == '\t' || id == '\r' || id == '\f' || id == '\v')
            {
                if (_char_list.contains(id))
                    return _char_list.at(id);
                _space_char._id = id;
                _space_char._width = 0.0f;
                _space_char._height = 0.0f;
                const f32 space_advance = _size > 0 ? static_cast<f32>(_size) * 0.25f : 16.0f;
                _space_char._xadvance = id == '\t' ? space_advance * 4.0f : space_advance;
                _space_char._u = _space_char._v = 0.0f;
                _space_char._twidth = _space_char._theight = 0.0f;
                _space_char._xoffset = _space_char._yoffset = 0.0f;
                _space_char._page = 0;
                return _space_char;
            }
            if (_char_list.contains(id))
            {
                return _char_list.at(id);
            }
            // 回退到'?'，如果'?'也不存在则返回第一个可用字符
            if (_char_list.contains('?'))
                return _char_list.at('?');
            if (!_char_list.empty())
                return _char_list.begin()->second;
            return _space_char; // 最终兜底
        }
        static Ref<Font> Create(const WString& file_path);
        //create from msdf
        static Ref<Font> Create(const Path &bitmap_path, const Path &json_path);
    };

    struct GlyphRenderInfo
    {
        char _c;
        Vector2f _pos;    // 左上角屏幕坐标
        Vector2f _size;   // scaled width/height
        Vector2f _uv;     // u,v起点
        Vector2f _uv_size;// u,v宽高
        u32 _page;
        f32 _xadvance;// 光标前进量
    };

    struct TextLayoutResult
    {
        Vector<GlyphRenderInfo> _glyphs;
        Vector2f _size = Vector2f::kZero;
    };

    TextLayoutResult LayoutText(const String &text, Vector2f pos, f32 font_size, Vector2f scale, Vector2f padding, Font *font);

}// namespace Ailu

#endif//AILU_FONT_H
