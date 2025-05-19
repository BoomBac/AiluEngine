//
// Created by 22292 on 2024/10/24.
//
#ifndef __TEXTRENDERER_H__
#define __TEXTRENDERER_H__
#include "GlobalMarco.h"
#include "Buffer.h"
#include "Material.h"
#include "Font.h"

#undef DrawText
namespace Ailu::Render
{
    class CommandBuffer;
    struct TextVertex
    {
        TextVertex() = default;
        TextVertex(const Vector4f& pos, const Vector4f& color, const Vector4f& uv)
            :_pos(pos), _color(color), _uv(uv) {}
        //object pos and padding
        Vector4f _pos;
        //top left's uv and char size in tex
        Vector4f _uv;
        Vector4f _color;
    };
    class AILU_API TextRenderer
    {
    public:
        static void Init();
        static void Shutdown();
        static TextRenderer* Get();
    public:
        inline static u16 kMaxCharacters = 1024u;
        TextRenderer();
        void Initialize();
        ~TextRenderer();
        //push a text
        static void DrawText(const String &text, Vector2f pos,u16 font_size = 14u,Vector2f scale = Vector2f::kOne,Color color = Colors::kWhite,Font *font = Get()->GetDefaultFont());
        static Vector2f CalculateTextSize(const String &text, Font *font = Get()->GetDefaultFont(),u16 font_size = 14u, Vector2f scale = Vector2f::kOne);
        void Render(RenderTexture* target, CommandBuffer * cmd);
        void Render(RenderTexture* target);
        bool _is_draw_debug_line = true;
        [[nodiscard]] Font* GetDefaultFont() const;
    private:
        Ref<Font> _default_font;
        u16 _current_frame_index = 0u;
        struct DrawerBlock
        {
            DISALLOW_COPY_AND_ASSIGN(DrawerBlock)
            Ref<Material> _default_mat;
            VertexBuffer* _vbuf;
            IndexBuffer* _ibuf;
            ConstantBuffer * _obj_cb;
            Vector<Vector3f> _pos_stream;
            Vector<Vector2f> _uv_stream;
            Vector<Vector4f> _color_stream;
            Vector<u32> _indices;
            u16 _characters_count = 0;
            explicit DrawerBlock(u32 char_num = kMaxCharacters);
            ~DrawerBlock();
            DrawerBlock& operator=(DrawerBlock&& other) noexcept;
            void AppendText(Font *font, const String &text, Vector2f pos, u16 font_size = 14u,Vector2f scale = Vector2f::kOne, Vector2f padding = Vector2f::kOne, Color color = Colors::kWhite);
            void Render(RenderTexture *target, CommandBuffer *cmd,bool is_debug);
            void ClearBuffer();
        };
        using FrameDrawerBlock = Map<WString,DrawerBlock*>;
        Array<FrameDrawerBlock,RenderConstants::kFrameCount> _drawer_blocks;
    };
}
#endif//__TEXTRENDERER_H__
