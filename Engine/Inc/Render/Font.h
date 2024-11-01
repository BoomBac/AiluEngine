//
// Created by 22292 on 2024/10/24.
//

#ifndef AILU_FONT_H
#define AILU_FONT_H
#include "GlobalMarco.h"
#include "Texture.h"
namespace Ailu
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
        bool _is_bold;
        bool _is_italic;
        bool _is_packed;
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
            if (_char_list.contains(id))
            {
                return _char_list.at(id);
            }
            char replace_char = '?';
            return _char_list.at(replace_char);
        }
        static Ref<Font> Create(const WString& file_path);
    };

}// namespace Ailu

#endif//AILU_FONT_H
