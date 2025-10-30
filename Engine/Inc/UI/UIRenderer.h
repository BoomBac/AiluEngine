//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_UIRENDERER_H
#define AILU_UIRENDERER_H
#include "Framework/Math/Transform2D.h"
#include "GlobalMarco.h"
#include "DrawerBlock.h"

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
        class TextRenderer;

        struct ImageDrawOptions
        {
            Matrix4x4f _transform = Matrix4x4f::Identity();// 可选的世界变换
            Vector4f _uv_rect = {0.f, 0.f, 1.f, 1.f};     // UV 坐标（左上 + 宽高，归一化）
            Color _tint = Colors::kWhite;                 // 颜色混合
            f32 _depth = 0.0f;                            // Z 深度排序
            Vector2f _size_override = {0.f, 0.f};         // 手动指定绘制大小（默认取纹理大小）
        };

        class AILU_API UIRenderer
        {
        public:
            static void Init();
            static void Shutdown();
            static UIRenderer* Get();
            
        public:
            UIRenderer();
            ~UIRenderer();
            void Render(CommandBuffer* cmd);
            void DrawQuad(Vector4f rect, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawQuad(Vector4f rect, Matrix4x4f matrix, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawText(const String &text, Vector2f pos, u16 font_size = 14u, Color color = Colors::kWhite,Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            void DrawText(const String &text, Vector2f pos, Matrix4x4f matrix,u16 font_size = 14u, Color color = Colors::kWhite,Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            void DrawImage(Render::Texture *texture, Vector4f rect, const ImageDrawOptions &opts = {});
            void DrawLine(Vector2f a, Vector2f b, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawLine(Vector2f a, Vector2f b, Matrix4x4f matrix,f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawBox(Vector2f pos, Vector2f size, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawBox(Vector2f pos, Vector2f size, Matrix4x4f matrix, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void PushScissor(Vector4f scissor);
            void PopScissor();
            Vector2f CalculateTextSize(const String &text, u16 font_size = 14u, Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            //统一走这个接口方便设置裁切矩形
            void AppendNode(DrawerBlock* block,u32 vert_num, u32 index_num, Render::Material *mat, Render::Texture *tex = nullptr);
        private:
            Vector<struct DrawerBlock *> &FrameBlocks() {return _drawer_blocks[_frame_index];};
            DrawerBlock *GetAvailableBlock(u32 vert_num,u32 index_num);
            void SubmitBlock(DrawerBlock *block, CommandBuffer *cmd,RenderTexture* color,RenderTexture* depth = nullptr);
        private:
            inline static const Matrix4x4f kIdentityMatrix = Matrix4x4f::Identity();
            u16 _frame_index = 0u;
            Ref<ConstantBuffer> _obj_cb;
            Scope<TextRenderer> _text_renderer;
            Array<Vector<DrawerBlock *>, Render::RenderConstants::kFrameCount> _drawer_blocks;
            DrawerBlock *_text_block;
            //暂时每个widget独立一个block,0保留为全局绘制
            u16 _cur_widget_index = 0u;
            Ref<Material> _default_material;
            HashMap<u64, u32> _on_sort_order_changed_handle_map;
            Vector<Rect> _scissor_stack;//stack一直报错？
        };

    }// namespace UI
}// namespace Aliu

#endif//AILU_UIRENDERER_H
