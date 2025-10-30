//
// Created by 22292 on 2024/10/24.
//
#ifndef __TEXTRENDERER_H__
#define __TEXTRENDERER_H__
#include "GlobalMarco.h"
#include "DrawerBlock.h"
#include "Render/Font.h"

#undef DrawText
namespace Ailu
{
    namespace Render
    {
        class CommandBuffer;
    }
    namespace UI
    {
        using Render::Font;
        using Render::RenderTexture;
        class AILU_API TextRenderer
        {
        public:
            inline static u16 kMaxCharacters = 1024u;
            static Vector2f CalculateTextSize(const String &text, u16 font_size = 14u, Font *font = nullptr, Vector2f scale = Vector2f::kOne);
        public:
            static Font *GetDefaultFont() { return s_default_font; };
            TextRenderer();
            void Initialize();
            ~TextRenderer();
            /// <summary>
            /// 独立绘制，用于简单gui
            /// </summary>
            void DrawText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Color color, Font *font = nullptr);
            /// <summary>
            /// 外部传入drawerblock，用于UI系统中批量绘制
            /// </summary>
            void DrawText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Color color, Font *font,DrawerBlock* block);
            void DrawText(const String &text, Vector2f pos, u16 font_size, Vector2f scale, Color color,Matrix4x4f matrix, Font *font,DrawerBlock* block);

            void Render(RenderTexture *target, Render::CommandBuffer *cmd);
            void Render(RenderTexture *target, Render::CommandBuffer *cmd, DrawerBlock *b);
            //适用于外部设置好目标和矩阵
            void Render(Render::CommandBuffer *cmd);
            bool _is_draw_debug_line = true;
        private:
            void AppendText(const String &text, Vector2f pos,Matrix4x4f matrix, u16 font_size, Vector2f scale, Color color, Vector2f padding,Font *font, DrawerBlock *block);
        private:
            inline static Font* s_default_font;
            Ref<Render::Material> _default_mat;
            DrawerBlock *_default_block = nullptr;
        };
    }// namespace UI
}// namespace Ailu::Render
#endif//__TEXTRENDERER_H__
