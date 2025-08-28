//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_UIRENDERER_H
#define AILU_UIRENDERER_H
#include "Framework/Math/ALMath.hpp"
#include "GlobalMarco.h"
#include "Render/Buffer.h"
#include "Render/Material.h"
#include "Render/Shader.h"

#undef DrawText

namespace Ailu
{
    using Render::RTHandle;
    using Render::RenderTexture;
    using Render::Material;
    using Render::VertexBuffer;
    using Render::IndexBuffer;
    using Render::ConstantBuffer;
    using Render::CommandBuffer;

    namespace Render
    {
        struct Font;
    };

    namespace UI
    {
        class Widget;
        class AILU_API UIRenderer
        {
        public:
            static void Init();
            static void Shutdown();
            static UIRenderer* Get();
            
        public:
            struct DrawerBlock
            {
            public:
                DISALLOW_COPY_AND_ASSIGN(DrawerBlock)
                DrawerBlock(DrawerBlock &&other) noexcept;
                DrawerBlock &operator=(DrawerBlock &&other) noexcept;
                explicit DrawerBlock(u32 vert_num = 1000u);
                ~DrawerBlock();

            public:
                Ref<Material> _mat;
                VertexBuffer *_vbuf;
                IndexBuffer *_ibuf;
                Vector<Vector3f> _pos_buf;
                Vector<Vector2f> _uv_buf;
                Vector<Color> _color_buf;
                Vector<u32> _index_buf;
                u32 _cur_vert_num = 0u;
                u32 _cur_index_num = 0u;
                u32 _max_vert_num = 0u;
            };
        public:
            UIRenderer();
            ~UIRenderer();
            void Render(CommandBuffer* cmd);
            void AddWidget(Ref<Widget> w);
            void RemoveWidget(Widget *w);
            //delete
            void DeleteWidget(Widget *w);

            void DrawQuad(Vector2f position, Vector2f size, f32 depth, Color color = Colors::kWhite);
            void DrawQuad(Vector4f rect,f32 depth, Color color = Colors::kWhite);
            void DrawText(const String &text, Vector2f pos,u16 font_size,Vector2f scale,Color color,Render::Font *font = nullptr);

            void DrawLine(Vector2f a, Vector2f b, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawBox(Vector2f pos, Vector2f size, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
        private:
            Vector<struct DrawerBlock *> &FrameBlocks() {return _drawer_blocks[_frame_index];};
            DrawerBlock *GetAvailableBlock(u32 vert_num);
        private:
            u16 _frame_index = 0u;
            Ref<ConstantBuffer> _obj_cb;
            Array<Vector<DrawerBlock *>, Render::RenderConstants::kFrameCount> _drawer_blocks;
            Vector<Ref<Widget>> _widgets;
            Vector<Ref<Widget>> _active_widgets;
            //暂时每个widget独立一个block
            u16 _cur_widget_index = 0u;
        };

    }// namespace UI
}// namespace Aliu

#endif//AILU_UIRENDERER_H
