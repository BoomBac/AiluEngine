//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_UIRENDERER_H
#define AILU_UIRENDERER_H
#include "Framework/Math/Transform2D.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/Array.h"
#include "DrawerBlock.h"
#include "Style/UIStyleBasic.h"
#include "Style/UIStyles.h"

#undef DrawText

namespace Ailu
{
    class Window;
    using Render::RTHandle;
    using Render::RenderTexture;
    using Render::Material;
    using Render::VertexBuffer;
    using Render::IndexBuffer;
    using Render::ConstantBuffer;
    using Render::CommandBuffer;

    namespace Render
    {
        class ComputeShader;
        struct Font;
        struct TextLayoutResult;
    };

    namespace UI
    {
        class Widget;
        class UIElement;
        class TextRenderer;

        struct ImageDrawOptions
        {
            Matrix4x4f _transform = Matrix4x4f::Identity();// 可选的世界变换
            Vector4f _uv_rect = {0.f, 0.f, 1.f, 1.f};     // UV 坐标（左上 + 宽高，归一化）
            Color _tint = Colors::kWhite;                 // 颜色混合
            f32 _depth = 0.0f;                            // Z 深度排序
            Vector2f _size_override = {0.f, 0.f};         // 手动指定绘制大小（默认取纹理大小）
        };

        struct UIRenderStats
        {
            u64 _ui_element_visit_count = 0u;
            u64 _ui_render_impl_count = 0u;
            u64 _ui_layout_count = 0u;
            u64 _ui_text_layout_count = 0u;
            u64 _ui_generated_vertex_count = 0u;
            u64 _ui_generated_index_count = 0u;
            u64 _ui_uploaded_bytes = 0u;
            u64 _ui_draw_node_count = 0u;
            u64 _ui_draw_call_count = 0u;
            u64 _ui_cache_hit_count = 0u;
            u64 _ui_cache_miss_count = 0u;
            f32 _ui_paint_build_time = 0.0f;
            f32 _ui_gpu_upload_time = 0.0f;
            f32 _ui_submit_time = 0.0f;
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
            void DrawQuad(Vector4f rect, const UIBrush& brush, f32 depth = 0.0f);
            void DrawQuad(Vector4f rect, const UIBrush& brush, Vector4f corner_radius, f32 depth = 0.0f);
            void DrawQuad(Vector4f rect, Matrix4x4f matrix, const UIBrush& brush, f32 depth = 0.0f);
            void DrawQuad(Vector4f rect, Matrix4x4f matrix, const UIBrush& brush, Vector4f corner_radius, f32 depth = 0.0f);
            void DrawWindowQuad(Window *window, Vector4f rect, const UIBrush &brush, f32 depth = 0.0f);
            void DrawWindowQuad(Window *window, Vector4f rect, const UIBrush &brush, Vector4f corner_radius, f32 depth = 0.0f);
            void DrawWindowText(Window *window, const String &text, Vector2f pos, f32 font_size = 14u, Color color = Colors::kWhite,
                                Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            void DrawVisual(Vector4f rect, Matrix4x4f matrix, const UIControlVisual &visual);
            void DrawText(const String &text, Vector2f pos, f32 font_size = 14u, Color color = Colors::kWhite,Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            void DrawText(const String &text, Vector2f pos, Matrix4x4f matrix,f32 font_size = 14u, Color color = Colors::kWhite,Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            void DrawTextLayout(const Render::TextLayoutResult &layout, Vector2f pos, Matrix4x4f matrix, f32 font_size = 14u, Color color = Colors::kWhite, Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            void DrawImage(Render::Texture *texture, Vector4f rect, const ImageDrawOptions &opts = {});
            void DrawLine(Vector2f a, Vector2f b, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawLine(Vector2f a, Vector2f b, Matrix4x4f matrix,f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawBox(Vector2f pos, Vector2f size, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void DrawBox(Vector2f pos, Vector2f size, Matrix4x4f matrix, f32 thickness = 1.0f, Color color = Colors::kWhite, f32 depth = 0.0f);
            void PushScissor(Vector4f scissor);
            void PopScissor();
            Vector2f CalculateTextSize(const String &text, u16 font_size = 14u, Vector2f scale = Vector2f::kOne, Render::Font *font = nullptr);
            //统一走这个接口方便设置裁切矩形
            void AppendNode(DrawerBlock *block, u32 vert_num, u32 index_num, Render::Material *mat, Render::Texture *tex = nullptr,
                            f32 msdf_px_range = 0.0f, bool is_backdrop_blur = false);
            void ResetFrameStats() { _stats = {}; }
            UIRenderStats &MutableStats() { return _stats; }
            const UIRenderStats &GetFrameStats() const { return _stats; }
        private:
            void DrawDebugPannel();
            Vector<struct DrawerBlock *> &FrameBlocks() {return _drawer_blocks[_frame_index];};
            struct WidgetDrawerBlocks;
            WidgetDrawerBlocks *GetWidgetBlockEntry(Widget *widget);
            Vector<DrawerBlock *> &GetWidgetCpuBlocks(Widget *widget);
            Vector<DrawerBlock *> &GetWidgetFrameBlocks(Widget *widget);
            void SyncWidgetFrameBlocks(Widget *widget);
            DrawerBlock *GetAvailableBlock(u32 vert_num,u32 index_num);
            DrawerBlock *GetAvailableWindowBlock(Window *window, u32 vert_num, u32 index_num);
            void DrawDirtyStateOverlay(UIElement *root);
            void DrawDirtyStateOverlayRecursive(UIElement *element, DrawerBlock *block);
            void AppendQuadToBlock(DrawerBlock *block, Vector4f rect, Matrix4x4f matrix, const UIBrush &brush,
                                   Vector4f corner_radius, f32 depth);
            Render::Texture *GetOrCreateBackdropBlurTexture(Render::Texture *source, CommandBuffer *cmd);
            void SubmitPopupBackdrop(Widget *widget, CommandBuffer *cmd, RenderTexture *color, RenderTexture *depth);
            void SubmitBlock(DrawerBlock *block, CommandBuffer *cmd,RenderTexture* color,RenderTexture* depth = nullptr);
        private:
            inline static const Matrix4x4f kIdentityMatrix = Matrix4x4f::Identity();
            u16 _frame_index = 0u;
            Ref<ConstantBuffer> _obj_cb;
            Scope<TextRenderer> _text_renderer;
            Array<Vector<DrawerBlock *>, Render::RenderConstants::kFrameCount> _drawer_blocks;
            Array<HashMap<Window *, Vector<DrawerBlock *>>, Render::RenderConstants::kFrameCount> _window_drawer_blocks;
            struct WidgetDrawerBlocks
            {
                Widget *_widget = nullptr;
                Vector<DrawerBlock *> _cpu_blocks = {};
                Array<Vector<DrawerBlock *>, Render::RenderConstants::kFrameCount> _blocks = {};
                Array<u64, Render::RenderConstants::kFrameCount> _gpu_revisions = {};
                u64 _build_revision = 0u;
            };
            Vector<WidgetDrawerBlocks> _widget_drawer_blocks;
            DrawerBlock *_text_block;
            DrawerBlock *_popup_backdrop_block;
            //暂时每个widget独立一个block,0保留为全局绘制
            u16 _cur_widget_index = 0u;
            DrawerBlock *_cur_widget_block = nullptr;
            Vector<DrawerBlock *> *_cur_widget_blocks = nullptr;
            u32 _cur_widget_block_index = 0u;
            Ref<Material> _default_material;
            Ref<Render::ComputeShader> _backdrop_blur_cs;
            Render::ComputeShaderKernelId _backdrop_blur_x_kernel = Render::kInvalidComputeShaderKernelId;
            Render::ComputeShaderKernelId _backdrop_blur_y_kernel = Render::kInvalidComputeShaderKernelId;
            Vector<RTHandle> _pending_backdrop_blur_release_handles;
            HashMap<Render::Texture *, Render::Texture *> _frame_backdrop_blur_cache;
            bool _cache_build_pending_resource = false;
            HashMap<u64, u32> _on_sort_order_changed_handle_map;
            Vector<Rect> _scissor_stack;//stack一直报错？
            UIRenderStats _stats;
        };

    }// namespace UI
}// namespace Aliu

#endif//AILU_UIRENDERER_H
