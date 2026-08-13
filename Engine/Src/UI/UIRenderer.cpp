//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/UIRenderer.h"
#include "Framework/Common/Profiler.h"
#include "Render/CommandBuffer.h"
#include "UI/TextRenderer.h"
#include "UI/Widget.h"
#include "UI/UIFramework.h"
#include "UI/DragDrop.h"
#include <Framework/Common/Allocator.hpp>
#include <Framework/Common/ResourceMgr.h>
#include "Render/Shader.h"
#include "Render/GraphicsContext.h"
#include <chrono>

namespace Ailu
{
    namespace UI
    {
        using namespace Render;
        static UIRenderer *s_Renderer = nullptr;
        namespace
        {
            constexpr u16 kBackdropBlurDownsample = 1u;
            constexpr u16 kMaxTempRTDimension = 4095u;
            using UIClock = std::chrono::high_resolution_clock;

            f32 ElapsedMs(UIClock::time_point start)
            {
                return std::chrono::duration<f32, std::milli>(UIClock::now() - start).count();
            }
        }

        void UIRenderer::Init()
        {
            s_Renderer = AL_NEW(UIRenderer);
        }
        void UIRenderer::Shutdown()
        {
            AL_DELETE(s_Renderer);
        }
        UIRenderer *UIRenderer::Get()
        {
            return s_Renderer;
        }
        UIRenderer::UIRenderer()
        {
            _obj_cb.reset(ConstantBuffer::Create(Render::RenderConstants::kPerObjectDataSize));
            _default_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_ui.alasset"), "DefaultUIMaterial");
            _default_material->SetTexture("_MainTex", Render::Texture::s_p_default_white);
            _shadow_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/ui_shadow.alasset"), "UIShadowMaterial");
            _shadow_material->SetFloat("_ShadowSpread", 16.0f);
            _backdrop_blur_cs = ComputeShader::Create(ResourceMgr::GetResSysPath(L"Shaders/hlsl/Compute/blur.hlsl"));
            _backdrop_blur_x_kernel = _backdrop_blur_cs->FindKernel("blur_x");
            _backdrop_blur_y_kernel = _backdrop_blur_cs->FindKernel("blur_y");
            for (auto &frame_blocks: _drawer_blocks)
            {
                frame_blocks.push_back(AL_NEW(DrawerBlock, _default_material,9600u));
            }
            _text_block = AL_NEW(DrawerBlock,MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_text.alasset"), "DefaultTextMaterial"));
            _popup_backdrop_block = AL_NEW(DrawerBlock, _default_material, 8u);
            _text_renderer = MakeScope<TextRenderer>();
        }
        UIRenderer::~UIRenderer()
        {
            for (auto &frame_blocks: _drawer_blocks)
            {
                for (auto b: frame_blocks)
                {
                    AL_DELETE(b);
                }
            }
            for (auto &entry: _widget_drawer_blocks)
            {
                for (auto *block: entry._cpu_blocks)
                    AL_DELETE(block);
                for (auto &frame_blocks: entry._blocks)
                {
                    for (auto *block: frame_blocks)
                        AL_DELETE(block);
                }
            }
            for (auto &frame_window_blocks: _window_drawer_blocks)
            {
                for (auto &it: frame_window_blocks)
                {
                    for (auto b: it.second)
                    {
                        AL_DELETE(b);
                    }
                }
            }
            AL_DELETE(_text_block);
            AL_DELETE(_popup_backdrop_block);
        }

        void UIRenderer::Render(CommandBuffer *cmd)
        {
            _frame_index = Render::g_pGfxContext != nullptr ? static_cast<u16>(Render::g_pGfxContext->GetFrameCount() % RenderConstants::kFrameCount) : 0u;
            ResetFrameStats();
            if (!_drawer_blocks[_frame_index].empty() && _drawer_blocks[_frame_index][0u] != nullptr)
                _drawer_blocks[_frame_index][0u]->ResetBuildData();
            for (auto &it: _window_drawer_blocks[_frame_index])
            {
                for (auto *block: it.second)
                {
                    if (block != nullptr)
                        block->ResetBuildData();
                }
            }
            _frame_backdrop_blur_cache.clear();

            if (auto selected = UIManager::Get()->GetDebugHighlightTarget(); selected != nullptr && selected->IsVisible())
            {
                DrawBox(selected->GetArrangeRect().xy, selected->GetArrangeRect().zw, 2.0f, Color(0.1f, 0.85f, 1.0f, 1.0f));
            }
            //DrawDebugPannel();
            const f32 dt = TimeMgr::s_delta_time;
            UI::UIManager::Get()->EnsurePopupWidgetsOnTop();
            auto& widgets = UI::UIManager::Get()->_widgets;
            Vector<Widget *> visible_widgets;
            visible_widgets.reserve(widgets.size());
            for (auto &widget: widgets)
            {
                auto *canvas = widget.get();
                if (canvas->_visibility == EVisibility::kVisible)
                    visible_widgets.push_back(canvas);
            }
            for (auto it = _widget_drawer_blocks.begin(); it != _widget_drawer_blocks.end();)
            {
                bool is_alive = false;
                for (auto &widget: widgets)
                {
                    if (widget.get() == it->_widget)
                    {
                        is_alive = true;
                        break;
                    }
                }
                if (is_alive)
                {
                    ++it;
                    continue;
                }
                for (auto *block: it->_cpu_blocks)
                    AL_DELETE(block);
                it->_cpu_blocks.clear();
                for (auto &frame_blocks: it->_blocks)
                {
                    for (auto *block: frame_blocks)
                        AL_DELETE(block);
                    frame_blocks.clear();
                }
                it = _widget_drawer_blocks.erase(it);
            }
            const bool is_debug_reflector_visible = UIManager::Get()->IsDebugReflectorVisible();
            if (is_debug_reflector_visible)
            {
                for (auto *canvas: visible_widgets)
                {
                    if (canvas->Root() != nullptr)
                        canvas->Root()->ClearDebugPaintDirtyRecursive();
                }
            }
            Vector<Widget *> updated_widgets;
            updated_widgets.reserve(visible_widgets.size());
            for (auto *canvas: visible_widgets)
            {
                canvas->PreUpdate(dt);
                updated_widgets.push_back(canvas);
            }
            for (auto *canvas: visible_widgets)
                canvas->Update(dt);
            UI::UIManager::Get()->EnsurePopupWidgetsOnTop();
            visible_widgets.clear();
            visible_widgets.reserve(widgets.size());
            for (auto &widget: widgets)
            {
                auto *canvas = widget.get();
                if (canvas->_visibility == EVisibility::kVisible)
                    visible_widgets.push_back(canvas);
            }
            for (auto *canvas: visible_widgets)
            {
                if (std::find(updated_widgets.begin(), updated_widgets.end(), canvas) != updated_widgets.end())
                    continue;
                if (is_debug_reflector_visible && canvas->Root() != nullptr)
                    canvas->Root()->ClearDebugPaintDirtyRecursive();
                canvas->PreUpdate(dt);
                canvas->Update(dt);
                updated_widgets.push_back(canvas);
            }
            DragDropManager::Get().Update();
            struct WidgetSubmitEntry
            {
                Widget *_widget = nullptr;
                RenderTexture *_color = nullptr;
                RenderTexture *_depth = nullptr;
            };
            Vector<WidgetSubmitEntry> submit_entries;
            submit_entries.reserve(visible_widgets.size());
            HashMap<RenderTexture *, RTHandle> composite_target_handles;
            auto get_submit_color = [&](RenderTexture *color) -> RenderTexture *
            {
                if (color == nullptr || !color->IsSwapChain())
                    return color;
                if (auto it = composite_target_handles.find(color); it != composite_target_handles.end())
                    return g_pRenderTexturePool->Get(it->second);
                RTHandle handle = cmd->GetTempRT(color->Width(), color->Height(), std::format("UI_Composite_{}", composite_target_handles.size()),
                                                 ERenderTargetFormat::kDefault, false, false, false);
                _pending_backdrop_blur_release_handles.push_back(handle);
                composite_target_handles[color] = handle;
                auto *composite = g_pRenderTexturePool->Get(handle);
                cmd->SetRenderTarget(composite);
                cmd->ClearRenderTarget(Colors::kBlack);
                cmd->SetRenderTargetLoadAction(composite, ELoadStoreAction::kLoad);
                return composite;
            };
            _cur_widget_index = 1u;
            for (auto *canvas: visible_widgets)
            {
                auto *entry = GetWidgetBlockEntry(canvas);
                auto &cpu_blocks = entry->_cpu_blocks;
                _cur_widget_blocks = &cpu_blocks;
                _cur_widget_block_index = 0u;
                _cur_widget_block = cpu_blocks.empty() ? nullptr : cpu_blocks[0u];
                bool is_cpu_blocks_empty = true;
                for (auto *block: cpu_blocks)
                    is_cpu_blocks_empty = is_cpu_blocks_empty && (block == nullptr || block->_nodes.empty());
                if (canvas->IsPaintCacheDirty(_frame_index) || is_cpu_blocks_empty)
                {
                    for (auto *block: cpu_blocks)
                    {
                        if (block != nullptr)
                            block->ResetBuildData();
                    }
                    _cur_widget_block = cpu_blocks.empty() ? nullptr : cpu_blocks[0u];
                    _cache_build_pending_resource = false;
                    const auto paint_build_start = UIClock::now();
                    canvas->Render(*this);
                    _stats._ui_paint_build_time += ElapsedMs(paint_build_start);
                    while (!cpu_blocks.empty() && (cpu_blocks.back() == nullptr || cpu_blocks.back()->_nodes.empty()))
                    {
                        AL_DELETE(cpu_blocks.back());
                        cpu_blocks.pop_back();
                    }
                    if (is_debug_reflector_visible && canvas->Root() != nullptr)
                        canvas->Root()->SnapshotPaintDirtyToDebugRecursive();
                    ++entry->_build_revision;
                    entry->_gpu_revisions.fill(0u);
                    if (!_cache_build_pending_resource)
                    {
                        for (u16 frame_index = 0u; frame_index < RenderConstants::kFrameCount; ++frame_index)
                            canvas->ClearPaintInvalidation(frame_index);
                    }
                    if (is_debug_reflector_visible)
                        DrawDirtyStateOverlay(canvas->Root());
                    ++_stats._ui_cache_miss_count;
                }
                else
                {
                    if (is_debug_reflector_visible)
                        DrawDirtyStateOverlay(canvas->Root());
                    ++_stats._ui_cache_hit_count;
                }
                SyncWidgetFrameBlocks(canvas);
                auto &blocks = GetWidgetFrameBlocks(canvas);
                bool has_nodes = false;
                for (auto *block: blocks)
                    has_nodes = has_nodes || (block != nullptr && !block->_nodes.empty());
                if (!has_nodes)
                {
                    ++_cur_widget_index;
                    continue;
                }

                auto [color, depth] = canvas->GetOutput();
                submit_entries.push_back({canvas, color, depth});
                ++_cur_widget_index;
            }
            _cur_widget_block = nullptr;
            _cur_widget_blocks = nullptr;
            _cur_widget_block_index = 0u;
            _cur_widget_index = 0u;
            if (_overlay_draw_callback)
                _overlay_draw_callback();
            for (const auto &entry: submit_entries)
            {
                if (entry._widget == nullptr || entry._widget->IsPopup())
                    continue;
                auto *submit_color = get_submit_color(entry._color);
                for (auto *block: GetWidgetFrameBlocks(entry._widget))
                    SubmitBlock(block, cmd, submit_color, entry._depth);
            }
            //绘制全局gui
            SubmitBlock(_drawer_blocks[_frame_index][0u], cmd, get_submit_color(RenderTexture::s_backbuffer));
            auto &window_blocks = _window_drawer_blocks[_frame_index];
            for (auto &it: window_blocks)
            {
                auto *color = RenderTexture::WindowBackBuffer(it.first);
                auto *submit_color = get_submit_color(color);
                for (auto *block: it.second)
                {
                    if (!block || block->_nodes.empty())
                        continue;
                    if (submit_color)
                        SubmitBlock(block, cmd, submit_color);
                    else
                        block->ResetBuildData();
                }
            }
            for (const auto &entry: submit_entries)
            {
                if (entry._widget == nullptr || !entry._widget->IsPopup())
                    continue;
                auto *submit_color = get_submit_color(entry._color);
                SubmitPopupBackdrop(entry._widget, cmd, submit_color, entry._depth);
                for (auto *block: GetWidgetFrameBlocks(entry._widget))
                    SubmitBlock(block, cmd, submit_color, entry._depth);
            }
            for (auto &it: composite_target_handles)
            {
                auto *backbuffer = it.first;
                if (backbuffer != nullptr)
                    cmd->Blit(it.second, backbuffer);
            }
            for (RTHandle handle: _pending_backdrop_blur_release_handles)
                cmd->ReleaseTempRT(handle);
            _pending_backdrop_blur_release_handles.clear();
            _frame_backdrop_blur_cache.clear();
            //暂时所有文本都渲染到后备缓冲区
            //TextRenderer::Get()->Render(RenderTexture::s_backbuffer, cmd, _text_block);
            //_text_block->Clear();
        }

        void UIRenderer::DrawQuad(Vector4f rect, const UIBrush& brush, f32 depth)
        {
            DrawQuad(rect, kIdentityMatrix, brush, depth);
        }

        void UIRenderer::DrawQuad(Vector4f rect, const UIBrush& brush, Vector4f corner_radius, f32 depth)
        {
            DrawQuad(rect, kIdentityMatrix, brush, corner_radius, depth);
        }

        void UIRenderer::DrawQuad(Vector4f rect,Matrix4x4f matrix, const UIBrush& brush, f32 depth)
        {
            DrawQuad(rect, matrix, brush, Vector4f::kZero, depth);
        }

        void UIRenderer::DrawQuad(Vector4f rect, Matrix4x4f matrix, const UIBrush& brush, Vector4f corner_radius, f32 depth)
        {
            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            AppendQuadToBlock(cb, rect, matrix, brush, corner_radius, depth);
        }

        void UIRenderer::DrawWindowQuad(Window *window, Vector4f rect, const UIBrush &brush, f32 depth)
        {
            DrawWindowQuad(window, rect, brush, Vector4f::kZero, depth);
        }

        void UIRenderer::DrawWindowQuad(Window *window, Vector4f rect, const UIBrush &brush, Vector4f corner_radius, f32 depth)
        {
            if (!window)
            {
                DrawQuad(rect, brush, corner_radius, depth);
                return;
            }
            DrawerBlock *cb = GetAvailableWindowBlock(window, 4u, 6u);
            AppendQuadToBlock(cb, rect, kIdentityMatrix, brush, corner_radius, depth);
        }

        void UIRenderer::DrawWindowShadow(Window *window, Vector4f rect, Color color, Vector4f corner_radius)
        {
            if (window == nullptr || _shadow_material == nullptr)
                return;

            UIBrush brush;
            brush._type = EUIBrushType::kColor;
            brush._tint = color;
            auto *block = GetAvailableWindowBlock(window, 4u, 6u, _shadow_material.get());
            AppendQuadToBlock(block, rect, kIdentityMatrix, brush, corner_radius, 0.0f, _shadow_material.get());
        }

        void UIRenderer::DrawWindowText(Window *window, const String &text, Vector2f pos, f32 font_size, Color color,
                                        Vector2f scale, Render::Font *font)
        {
            if (!window)
            {
                DrawText(text, pos, font_size, color, scale, font);
                return;
            }
            _text_renderer->DrawText(text, pos, font_size, scale, color, font, GetAvailableWindowBlock(window, 4u, 6u));
        }

        void UIRenderer::AppendQuadToBlock(DrawerBlock *cb, Vector4f rect, Matrix4x4f matrix, const UIBrush &brush,
                                           Vector4f corner_radius, f32 depth, Render::Material *material,
                                           Vector4f border_thickness)
        {
            auto color = brush._tint;
            u32 cur_vert_num = cb->CurrentVertNum(), cur_index_num = cb->CurrentIndexNum();
            cb->_pos_buf[cur_vert_num] = {rect.xy, depth};
            cb->_pos_buf[cur_vert_num + 1] = {rect.x + rect.z, rect.y, depth};
            cb->_pos_buf[cur_vert_num + 2] = {rect.x, rect.y + rect.w, depth};
            cb->_pos_buf[cur_vert_num + 3] = {rect.x + rect.z, rect.y + rect.w, depth};
            TransformCoord(cb->_pos_buf[cur_vert_num], matrix);
            TransformCoord(cb->_pos_buf[cur_vert_num + 1], matrix);
            TransformCoord(cb->_pos_buf[cur_vert_num + 2], matrix);
            TransformCoord(cb->_pos_buf[cur_vert_num + 3], matrix);
            cb->_uv_buf[cur_vert_num] = brush._uv_rect.xy;
            cb->_uv_buf[cur_vert_num + 1] = {brush._uv_rect.x + brush._uv_rect.z, brush._uv_rect.y};
            cb->_uv_buf[cur_vert_num + 2] = {brush._uv_rect.x, brush._uv_rect.y + brush._uv_rect.w};
            cb->_uv_buf[cur_vert_num + 3] = {brush._uv_rect.x + brush._uv_rect.z, brush._uv_rect.y + brush._uv_rect.w};
            cb->_color_buf[cur_vert_num] = color;
            cb->_color_buf[cur_vert_num + 1] = color;
            cb->_color_buf[cur_vert_num + 2] = color;
            cb->_color_buf[cur_vert_num + 3] = color;
            cb->_rect_buf[cur_vert_num] = rect;
            cb->_rect_buf[cur_vert_num + 1] = rect;
            cb->_rect_buf[cur_vert_num + 2] = rect;
            cb->_rect_buf[cur_vert_num + 3] = rect;
            cb->_corner_radius_buf[cur_vert_num] = corner_radius;
            cb->_corner_radius_buf[cur_vert_num + 1] = corner_radius;
            cb->_corner_radius_buf[cur_vert_num + 2] = corner_radius;
            cb->_corner_radius_buf[cur_vert_num + 3] = corner_radius;
            cb->_border_thickness_buf[cur_vert_num] = border_thickness;
            cb->_border_thickness_buf[cur_vert_num + 1] = border_thickness;
            cb->_border_thickness_buf[cur_vert_num + 2] = border_thickness;
            cb->_border_thickness_buf[cur_vert_num + 3] = border_thickness;
            cb->_index_buf[cur_index_num] = cur_vert_num + 0u;
            cb->_index_buf[cur_index_num + 1] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 2] = cur_vert_num + 2u;
            cb->_index_buf[cur_index_num + 3] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 4] = cur_vert_num + 3u;
            cb->_index_buf[cur_index_num + 5] = cur_vert_num + 2u;
            AppendNode(cb, 4u, 6u, material != nullptr ? material : _default_material.get(),
                       brush._texture ? brush._texture : Texture::s_p_default_white,
                       0.0f, brush._type == EUIBrushType::kBackdropBlur);
        }

        void UIRenderer::DrawVisual(Vector4f rect, Matrix4x4f matrix, const UIControlVisual &visual)
        {
            if (visual._background._type != EUIBrushType::kNone && visual._background._tint.a > 0.0f)
                DrawQuad(rect, matrix, visual._background, visual._corner_radius);
            if ((visual._border_width.x > 0.0f || visual._border_width.y > 0.0f ||
                 visual._border_width.z > 0.0f || visual._border_width.w > 0.0f) && visual._border_color.a > 0.0f)
                DrawBorder(rect, matrix, visual._border_width, visual._corner_radius, visual._border_color);
        }

        void UIRenderer::DrawText(const String &text, Vector2f pos, f32 font_size, Color color,Vector2f scale, Render::Font *font)
        {
            _text_renderer->DrawText(text, pos, font_size, scale, color, font, GetAvailableBlock(4u, 6u));
        }

        void UIRenderer::DrawText(const String &text, Vector2f pos, Matrix4x4f matrix, f32 font_size, Color color, Vector2f scale, Render::Font *font)
        {
            _text_renderer->DrawText(text, pos, font_size, scale, color, matrix, font, GetAvailableBlock(4u, 6u));
        }
        void UIRenderer::DrawTextLayout(const Render::TextLayoutResult &layout, Vector2f pos, Matrix4x4f matrix, f32 font_size, Color color, Vector2f scale, Render::Font *font)
        {
            _text_renderer->DrawTextLayout(layout, pos, font_size, scale, color, matrix, font, GetAvailableBlock(4u, 6u));
        }

        void UIRenderer::DrawImage(Render::Texture *texture, Vector4f rect, const ImageDrawOptions &opts)
        {
            if (!texture)
                return;
            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            u32 cur_vert_num = cb->CurrentVertNum(), cur_index_num = cb->CurrentIndexNum();
            cb->_pos_buf[cur_vert_num] = {rect.xy, opts._depth};
            cb->_pos_buf[cur_vert_num + 1] = {rect.x + rect.z, rect.y, opts._depth};
            cb->_pos_buf[cur_vert_num + 2] = {rect.x, rect.y + rect.w, opts._depth};
            cb->_pos_buf[cur_vert_num + 3] = {rect.x + rect.z, rect.y + rect.w, opts._depth};
            TransformCoord(cb->_pos_buf[cur_vert_num], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 1], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 2], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 3], opts._transform);
            cb->_uv_buf[cur_vert_num] = {0.f, 0.f};
            cb->_uv_buf[cur_vert_num + 1] = {1.f, 0.f};
            cb->_uv_buf[cur_vert_num + 2] = {0.f, 1.f};
            cb->_uv_buf[cur_vert_num + 3] = {1.f, 1.f};
            cb->_color_buf[cur_vert_num]     = opts._tint;
            cb->_color_buf[cur_vert_num + 1] = opts._tint;
            cb->_color_buf[cur_vert_num + 2] = opts._tint;
            cb->_color_buf[cur_vert_num + 3] = opts._tint;
            cb->_rect_buf[cur_vert_num] = rect;
            cb->_rect_buf[cur_vert_num + 1] = rect;
            cb->_rect_buf[cur_vert_num + 2] = rect;
            cb->_rect_buf[cur_vert_num + 3] = rect;
              cb->_corner_radius_buf[cur_vert_num] = opts._corner_radius;
              cb->_corner_radius_buf[cur_vert_num + 1] = opts._corner_radius;
              cb->_corner_radius_buf[cur_vert_num + 2] = opts._corner_radius;
              cb->_corner_radius_buf[cur_vert_num + 3] = opts._corner_radius;
            cb->_border_thickness_buf[cur_vert_num] = Vector4f::kZero;
            cb->_border_thickness_buf[cur_vert_num + 1] = Vector4f::kZero;
            cb->_border_thickness_buf[cur_vert_num + 2] = Vector4f::kZero;
            cb->_border_thickness_buf[cur_vert_num + 3] = Vector4f::kZero;
            cb->_index_buf[cur_index_num] = cur_vert_num + 0u;
            cb->_index_buf[cur_index_num + 1] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 2] = cur_vert_num + 2u;
            cb->_index_buf[cur_index_num + 3] = cur_vert_num + 1u;
            cb->_index_buf[cur_index_num + 4] = cur_vert_num + 3u;
            cb->_index_buf[cur_index_num + 5] = cur_vert_num + 2u;
            AppendNode(cb, 4u, 6u, _default_material.get(),texture);
        }

        void UIRenderer::DrawLine(Vector2f a, Vector2f b, f32 thickness, Color color, f32 depth)
        {
            DrawLine(a, b, kIdentityMatrix, thickness, color, depth);
        }
        void UIRenderer::DrawLine(Vector2f a, Vector2f b, Matrix4x4f matrix, f32 thickness, Color color, f32 depth)
        {
            if (Magnitude(b - a) <= 0.001f)
                return;
            // 线方向
            Vector2f dir = Normalize(b - a);
            // 法线（垂直方向）
            Vector2f normal = {-dir.y, dir.x};
            Vector2f offset = normal * (thickness * 0.5f);

            // 矩形的四个点
            Vector2f p0 = a - offset;
            Vector2f p1 = b - offset;
            Vector2f p2 = a + offset;
            Vector2f p3 = b + offset;

            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            auto v = cb->CurrentVertNum();
            auto i = cb->CurrentIndexNum();

            cb->_pos_buf[v + 0] = {p0, depth};
            cb->_pos_buf[v + 1] = {p1, depth};
            cb->_pos_buf[v + 2] = {p2, depth};
            cb->_pos_buf[v + 3] = {p3, depth};
            TransformCoord(cb->_pos_buf[v + 0], matrix);
            TransformCoord(cb->_pos_buf[v + 1], matrix);
            TransformCoord(cb->_pos_buf[v + 2], matrix);
            TransformCoord(cb->_pos_buf[v + 3], matrix);

            // uv 用不到，可以全 0
            cb->_uv_buf[v + 0] = {0, 0};
            cb->_uv_buf[v + 1] = {0, 0};
            cb->_uv_buf[v + 2] = {0, 0};
            cb->_uv_buf[v + 3] = {0, 0};

            cb->_color_buf[v + 0] = color;
            cb->_color_buf[v + 1] = color;
            cb->_color_buf[v + 2] = color;
            cb->_color_buf[v + 3] = color;
            const Vector4f rect = {p0.x, p0.y, b.x - a.x, thickness};
            cb->_rect_buf[v + 0] = rect;
            cb->_rect_buf[v + 1] = rect;
            cb->_rect_buf[v + 2] = rect;
            cb->_rect_buf[v + 3] = rect;
            cb->_corner_radius_buf[v + 0] = Vector4f::kZero;
            cb->_corner_radius_buf[v + 1] = Vector4f::kZero;
            cb->_corner_radius_buf[v + 2] = Vector4f::kZero;
            cb->_corner_radius_buf[v + 3] = Vector4f::kZero;
            cb->_border_thickness_buf[v + 0] = Vector4f::kZero;
            cb->_border_thickness_buf[v + 1] = Vector4f::kZero;
            cb->_border_thickness_buf[v + 2] = Vector4f::kZero;
            cb->_border_thickness_buf[v + 3] = Vector4f::kZero;

            cb->_index_buf[i + 0] = v + 0;
            cb->_index_buf[i + 1] = v + 1;
            cb->_index_buf[i + 2] = v + 2;
            cb->_index_buf[i + 3] = v + 1;
            cb->_index_buf[i + 4] = v + 3;
            cb->_index_buf[i + 5] = v + 2;
            AppendNode(cb, 4u, 6u, _default_material.get());
        }

        void UIRenderer::DrawBezier(Vector2f start, Vector2f start_tangent, Vector2f end_tangent, Vector2f end,
                                    f32 thickness, Color color, f32 depth, u32 segments)
        {
            segments = std::max(1u, segments);
            auto sample = [&](f32 t)
            {
                const f32 inv_t = 1.0f - t;
                return start * (inv_t * inv_t * inv_t) + start_tangent * (3.0f * inv_t * inv_t * t) +
                       end_tangent * (3.0f * inv_t * t * t) + end * (t * t * t);
            };

            Vector2f previous = start;
            for (u32 segment_index = 1u; segment_index <= segments; ++segment_index)
            {
                const f32 t = static_cast<f32>(segment_index) / static_cast<f32>(segments);
                Vector2f current = sample(t);
                DrawLine(previous, current, thickness, color, depth);
                previous = current;
            }
        }
        void UIRenderer::DrawBox(Vector2f pos, Vector2f size, f32 thickness, Color color, f32 depth)
        {
            DrawBox(pos, size, kIdentityMatrix, thickness, color, depth);
        }

        void UIRenderer::DrawBox(Vector2f pos, Vector2f size, Matrix4x4f matrix, f32 thickness, Color color, f32 depth)
        {
            Vector2f p0 = pos;
            Vector2f p1 = {pos.x + size.x, pos.y};
            Vector2f p2 = {pos.x + size.x, pos.y + size.y};
            Vector2f p3 = {pos.x, pos.y + size.y};

            DrawLine(p0, p1, matrix,thickness, color, depth);// top
            DrawLine(p1, p2, matrix,thickness, color, depth);// right
            DrawLine(p2, p3, matrix,thickness, color, depth);// bottom
            DrawLine(p3, p0, matrix,thickness, color, depth);// left
        }

        void UIRenderer::DrawBorder(Vector4f rect, Matrix4x4f matrix, Vector4f thickness, Vector4f corner_radius,
                                    Color color, f32 depth)
        {
            if ((thickness.x <= 0.0f && thickness.y <= 0.0f && thickness.z <= 0.0f && thickness.w <= 0.0f) ||
                color.a <= 0.0f)
                return;
            UIBrush brush;
            brush._type = EUIBrushType::kColor;
            brush._tint = color;
            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            AppendQuadToBlock(cb, rect, matrix, brush, corner_radius, depth, nullptr, thickness);
        }

        void UIRenderer::PushScissor(Vector4f scissor)
        {
            Clamp(scissor.x, 0.f, 65535.f);
            Clamp(scissor.y, 0.f, 65535.f);
            _scissor_stack.push_back(Rect((u16) scissor.x, (u16) scissor.y, (u16) (scissor.x + scissor.z), (u16) (scissor.y + scissor.w)));
        }

        void UIRenderer::PopScissor()
        {
            _scissor_stack.erase(_scissor_stack.end()-1);
        }

        Vector2f UIRenderer::CalculateTextSize(const String &text,u16 font_size, Vector2f scale, Render::Font *font)
        {
            return _text_renderer->CalculateTextSize(text, font_size, font, scale);
        }

        void UIRenderer::AppendNode(DrawerBlock *block, u32 vert_num, u32 index_num, Render::Material *mat, Render::Texture *tex,
                                    f32 msdf_px_range, bool is_backdrop_blur)
        {
            if (mat != nullptr && !mat->IsReadyForDraw())
                _cache_build_pending_resource = true;
            if (tex != nullptr && !tex->IsReady())
                _cache_build_pending_resource = true;
            if (!_scissor_stack.empty())
                block->AppendNode(vert_num, index_num, mat, tex, _scissor_stack.back(), msdf_px_range, is_backdrop_blur);
            else
                block->AppendNode(vert_num, index_num, mat, tex, {}, msdf_px_range, is_backdrop_blur);
            _stats._ui_generated_vertex_count += vert_num;
            _stats._ui_generated_index_count += index_num;
        }

        void UIRenderer::DrawDirtyStateOverlay(UIElement *root)
        {
            if (root == nullptr || _drawer_blocks[_frame_index].empty())
                return;
            DrawDirtyStateOverlayRecursive(root, _drawer_blocks[_frame_index][0u]);
        }

        void UIRenderer::DrawDirtyStateOverlayRecursive(UIElement *element, DrawerBlock *block)
        {
            if (element == nullptr || block == nullptr || !element->IsVisible())
                return;

            constexpr f32 kIndicatorSize = 6.0f;
            constexpr f32 kIndicatorInset = 1.0f;
            const Vector4f rect = element->GetArrangeRect();
            if (rect.z >= kIndicatorSize && rect.w >= kIndicatorSize && block->CanAppend(4u, 6u))
            {
                UIBrush brush;
                brush._type = EUIBrushType::kColor;
                brush._tint = element->IsDebugPaintDirty() ? Color(1.0f, 0.12f, 0.08f, 0.95f) : Color(0.1f, 0.85f, 0.25f, 0.95f);
                const Vector4f indicator_rect = {rect.x + rect.z - kIndicatorSize - kIndicatorInset, rect.y + kIndicatorInset,
                                                 kIndicatorSize, kIndicatorSize};
                AppendQuadToBlock(block, indicator_rect, kIdentityMatrix, brush, Vector4f::kZero, 0.0f);
            }

            for (const auto &child: element->GetChildren())
                DrawDirtyStateOverlayRecursive(child.get(), block);
        }

        UIRenderer::WidgetDrawerBlocks *UIRenderer::GetWidgetBlockEntry(Widget *widget)
        {
            AL_ASSERT(widget != nullptr);
            for (auto &entry: _widget_drawer_blocks)
            {
                if (entry._widget != widget)
                    continue;
                return &entry;
            }

            WidgetDrawerBlocks entry;
            entry._widget = widget;
            entry._cpu_blocks.push_back(AL_NEW(DrawerBlock, _default_material, 8092u * 4));
            for (auto &frame_blocks: entry._blocks)
                frame_blocks.push_back(AL_NEW(DrawerBlock, _default_material, 8092u * 4));
            _widget_drawer_blocks.push_back(entry);
            return &_widget_drawer_blocks.back();
        }

        Vector<DrawerBlock *> &UIRenderer::GetWidgetCpuBlocks(Widget *widget)
        {
            return GetWidgetBlockEntry(widget)->_cpu_blocks;
        }

        Vector<DrawerBlock *> &UIRenderer::GetWidgetFrameBlocks(Widget *widget)
        {
            return GetWidgetBlockEntry(widget)->_blocks[_frame_index];
        }

        void UIRenderer::SyncWidgetFrameBlocks(Widget *widget)
        {
            auto *entry = GetWidgetBlockEntry(widget);
            if (entry == nullptr || _frame_index >= RenderConstants::kFrameCount || entry->_gpu_revisions[_frame_index] == entry->_build_revision)
                return;
            auto &source_blocks = entry->_cpu_blocks;
            auto &target_blocks = entry->_blocks[_frame_index];
            while (target_blocks.size() < source_blocks.size())
                target_blocks.push_back(AL_NEW(DrawerBlock, _default_material, 8092u * 4));
            for (u32 block_index = 0u; block_index < source_blocks.size(); ++block_index)
            {
                if (source_blocks[block_index] == nullptr)
                {
                    AL_DELETE(target_blocks[block_index]);
                    target_blocks[block_index] = nullptr;
                    continue;
                }
                if (target_blocks[block_index] == nullptr)
                    target_blocks[block_index] = AL_NEW(DrawerBlock, _default_material, 8092u * 4);
                target_blocks[block_index]->CopyBuildDataFrom(*source_blocks[block_index]);
            }
            while (target_blocks.size() > source_blocks.size())
            {
                AL_DELETE(target_blocks.back());
                target_blocks.pop_back();
            }
            entry->_gpu_revisions[_frame_index] = entry->_build_revision;
        }

        DrawerBlock *UIRenderer::GetAvailableBlock(u32 vert_num, u32 index_num)
        {
            DrawerBlock *available_block = nullptr;
            if (_cur_widget_blocks != nullptr)
            {
                if (_cur_widget_block == nullptr)
                {
                    _cur_widget_block_index = 0u;
                    if (_cur_widget_blocks->empty())
                        _cur_widget_blocks->push_back(AL_NEW(DrawerBlock, _default_material, 8092u * 4));
                    _cur_widget_block = (*_cur_widget_blocks)[_cur_widget_block_index];
                }
                else if (!_cur_widget_block->CanAppend(vert_num, index_num))
                {
                    ++_cur_widget_block_index;
                    if (_cur_widget_block_index >= _cur_widget_blocks->size())
                    {
                        _cur_widget_blocks->push_back(AL_NEW(DrawerBlock, _default_material, 8092u * 4));
                    }
                    _cur_widget_block = (*_cur_widget_blocks)[_cur_widget_block_index];
                }
                AL_ASSERT_MSG(_cur_widget_block->CanAppend(vert_num, index_num), "UI DrawerBlock index overflow!");
                return _cur_widget_block;
            }
            auto &frame_block = _drawer_blocks[_frame_index];
            if (frame_block.size() < _cur_widget_index + 1u)
            {
                frame_block.push_back(AL_NEW(DrawerBlock, _default_material,8092u * 4));
                available_block = frame_block.back();
            }
            else
            {
                //for (auto b: frame_block)
                //{
                //    if (b->_cur_vert_num + vert_num <= b->_max_vert_num)
                //    {
                //        available_block = b;
                //        break;
                //    }
                //}
                //if (available_block == nullptr)
                //{
                //    frame_block.push_back(AL_NEW(DrawerBlock));
                //    available_block = frame_block.back();
                //}
                available_block = frame_block[_cur_widget_index];
                AL_ASSERT_MSG(available_block->CanAppend(vert_num,index_num), "UI DrawerBlock index overflow!");
            }
            return available_block;
        }
        DrawerBlock *UIRenderer::GetAvailableWindowBlock(Window *window, u32 vert_num, u32 index_num, Render::Material *material)
        {
            if (material == nullptr)
                material = _default_material.get();
            auto &blocks = _window_drawer_blocks[_frame_index][window];
            for (auto *block: blocks)
            {
                if (block->_mat.get() == material && block->CanAppend(vert_num, index_num))
                    return block;
            }
            Ref<Material> block_material = material == _shadow_material.get() ? _shadow_material : _default_material;
            blocks.push_back(AL_NEW(DrawerBlock, block_material, 8092u * 4));
            return blocks.back();
        }
        Render::Texture *UIRenderer::GetOrCreateBackdropBlurTexture(Render::Texture *source, CommandBuffer *cmd)
        {
            if (source == nullptr || _backdrop_blur_cs == nullptr)
                return source;
            if (auto it = _frame_backdrop_blur_cache.find(source); it != _frame_backdrop_blur_cache.end())
                return it->second;

            const u32 source_width = source->Width();
            const u32 source_height = source->Height();
            const u16 blur_width = static_cast<u16>(std::clamp(source_width / kBackdropBlurDownsample, 1u,
                                                                static_cast<u32>(kMaxTempRTDimension)));
            const u16 blur_height = static_cast<u16>(std::clamp(source_height / kBackdropBlurDownsample, 1u,
                                                                 static_cast<u32>(kMaxTempRTDimension)));
            RTHandle downsample = cmd->GetTempRT(blur_width, blur_height, "UI_BackdropBlur_Downsample", ERenderTargetFormat::kDefaultHDR, false, false, true);
            RTHandle blur_x = cmd->GetTempRT(blur_width, blur_height, "UI_BackdropBlur_X", ERenderTargetFormat::kDefaultHDR, false, false, true);
            RTHandle blur_y = cmd->GetTempRT(blur_width, blur_height, "UI_BackdropBlur_Y", ERenderTargetFormat::kDefaultHDR, false, false, true);
            _pending_backdrop_blur_release_handles.push_back(downsample);
            _pending_backdrop_blur_release_handles.push_back(blur_x);
            _pending_backdrop_blur_release_handles.push_back(blur_y);

            auto *downsample_rt = g_pRenderTexturePool->Get(downsample);
            auto *blur_x_rt = g_pRenderTexturePool->Get(blur_x);
            auto *blur_y_rt = g_pRenderTexturePool->Get(blur_y);

            cmd->StateTransition(downsample_rt, EResourceState::kRenderTarget);
            cmd->Blit(source, downsample);
            cmd->StateTransition(downsample_rt, EResourceState::kNonPixelShaderResource);
            cmd->StateTransition(blur_x_rt, EResourceState::kUnorderedAccess);
            _backdrop_blur_cs->SetTexture("_SourceTex", downsample);
            _backdrop_blur_cs->SetTexture("_OutTex", blur_x);
            auto [group_num_x, group_num_y, group_num_z] = _backdrop_blur_cs->CalculateDispatchNum(_backdrop_blur_x_kernel, blur_width, blur_height, 1u);
            cmd->Dispatch(_backdrop_blur_cs.get(), _backdrop_blur_x_kernel, group_num_x, group_num_y, 1u);
            cmd->InsertUAVBarrier(blur_x_rt);
            cmd->StateTransition(blur_x_rt, EResourceState::kNonPixelShaderResource);
            cmd->StateTransition(blur_y_rt, EResourceState::kUnorderedAccess);
            _backdrop_blur_cs->SetTexture("_SourceTex", blur_x);
            _backdrop_blur_cs->SetTexture("_OutTex", blur_y);
            cmd->Dispatch(_backdrop_blur_cs.get(), _backdrop_blur_y_kernel, group_num_x, group_num_y, 1u);
            cmd->InsertUAVBarrier(blur_y_rt);
            cmd->StateTransition(blur_y_rt, EResourceState::kPixelShaderResource);

            _frame_backdrop_blur_cache[source] = blur_y_rt;
            return blur_y_rt;
        }

        void UIRenderer::SubmitPopupBackdrop(Widget *widget, CommandBuffer *cmd, RenderTexture *color, RenderTexture *depth)
        {
            if (widget == nullptr || widget->Root() == nullptr || color == nullptr || _popup_backdrop_block == nullptr)
                return;
            const Vector4f rect = widget->Root()->GetArrangeRect();
            if (rect.z <= 1.0f || rect.w <= 1.0f)
                return;

            _popup_backdrop_block->ResetBuildData();
            _frame_backdrop_blur_cache.erase(color);

            const f32 w = static_cast<f32>(color->Width());
            const f32 h = static_cast<f32>(color->Height());
            UIBrush brush;
            brush._type = EUIBrushType::kBackdropBlur;
            brush._texture = color;
            brush._tint = Color(1.0f, 1.0f, 1.0f, 0.92f);
            brush._uv_rect = {rect.x / w, rect.y / h, rect.z / w, rect.w / h};
            AppendQuadToBlock(_popup_backdrop_block, rect, kIdentityMatrix, brush, Vector4f(6.0f), 0.0f);
            SubmitBlock(_popup_backdrop_block, cmd, color, depth);
        }

        void UIRenderer::SubmitBlock(DrawerBlock *b, CommandBuffer *cmd,RenderTexture* color,RenderTexture* depth)
        {
            if (color == nullptr)
                return;
            if (b == nullptr || b->_nodes.empty())
                return;
            if (!b->IsReady())
                return;
            f32 w = (f32) color->Width();
            f32 h = (f32) color->Height();
            CBufferPerCameraData cb_per_cam;
            Matrix4x4f view, proj;
            f32 aspect = w / h;
            f32 half_width = w * 0.5f, half_height = h * 0.5f;
            BuildViewMatrixLookToLH(view, Vector3f(0.f, 0.f, -50.f), Vector3f::kForward, Vector3f::kUp);
            BuildOrthographicMatrix(proj, 0.0f, w, 0.0f, h, 1.f, 200.f);
            cb_per_cam._MatrixVP = view * proj;
            cb_per_cam._ScreenParams = Vector4f(1.0f / w, 1.0f / h, w, h);
            CBufferPerObjectData per_obj_data;
            per_obj_data._MatrixWorld = BuildIdentityMatrix();
            memcpy(_obj_cb->GetData(), &per_obj_data, RenderConstants::kPerObjectDataSize);
            cmd->SetGlobalBuffer(RenderConstants::kCBufNamePerCamera, &cb_per_cam, RenderConstants::kPerCameraDataSize);
            Color tint = Colors::kWhite;
            b->_mat->SetVector("_Color", tint);
            if (b->_mat.get() == _shadow_material.get())
                b->_mat->SetFloat("_ShadowSpread", 16.0f);
            for (const auto &node: b->_nodes)
            {
                if (node._is_backdrop_blur)
                    GetOrCreateBackdropBlurTexture(node._main_tex, cmd);
            }
            cmd->SetRenderTarget(color, depth);
            const auto upload_start = UIClock::now();
            _stats._ui_uploaded_bytes += b->SubmitVertexData();
            _stats._ui_gpu_upload_time += ElapsedMs(upload_start);
            Rect full_rect(0u, 0u, (u16) w, (u16) h);
            Rect prev_scissor = full_rect;
            _stats._ui_draw_node_count += b->_nodes.size();
            const auto submit_start = UIClock::now();
            for (const auto& node: b->_nodes)
            {
                Render::Texture *main_tex = node._main_tex;
                if (node._is_backdrop_blur)
                {
                    if (auto it = _frame_backdrop_blur_cache.find(node._main_tex); it != _frame_backdrop_blur_cache.end())
                        main_tex = it->second;
                    else
                        main_tex = GetOrCreateBackdropBlurTexture(node._main_tex, cmd);
                }
                node._mat->SetTexture("_MainTex", main_tex ? main_tex : Texture::s_p_default_white);
                node._mat->SetFloat("_MsdfPxRange", node._msdf_px_range);
                if (node._is_custom_scissor)
                {
                    if (prev_scissor != node._scissor)
                        cmd->SetScissorRect(node._scissor);
                }
                else
                {
                    if (prev_scissor != full_rect)
                        cmd->SetScissorRect(full_rect);
                }
                cmd->DrawIndexed(b->_vbuf.get(), b->_ibuf.get(), _obj_cb.get(), node._mat,0u,node._index_offset,node._index_num);
                ++_stats._ui_draw_call_count;
                prev_scissor = node._is_custom_scissor ? node._scissor : full_rect;
            }
            _stats._ui_submit_time += ElapsedMs(submit_start);
        }

        void UIRenderer::DrawDebugPannel()
        {
            Vector2f pen = {10.f, 10.f};
            f32 font_size = 14.f;
            const f32 line_height = _text_renderer->GetDefaultFont()->_line_height * font_size;
            DrawText(std::format("UI Stats: visit {}, render {}, layout {}, text {}, vert {}, idx {}, upload {}KB, nodes {}, calls {}",
                                 _stats._ui_element_visit_count,
                                 _stats._ui_render_impl_count,
                                 _stats._ui_layout_count,
                                 _stats._ui_text_layout_count,
                                 _stats._ui_generated_vertex_count,
                                 _stats._ui_generated_index_count,
                                 _stats._ui_uploaded_bytes / 1024u,
                                 _stats._ui_draw_node_count,
                                 _stats._ui_draw_call_count),
                     pen, font_size);
            pen.y += line_height;
            DrawText(std::format("UI Time: paint {:.3f}ms, upload {:.3f}ms, submit {:.3f}ms, hit {}, miss {}",
                                 _stats._ui_paint_build_time,
                                 _stats._ui_gpu_upload_time,
                                 _stats._ui_submit_time,
                                 _stats._ui_cache_hit_count,
                                 _stats._ui_cache_miss_count),
                     pen, font_size);
            pen.y += line_height;
            UIElement *capture = UIManager::Get()->_capture_target;
            if (capture)
            {
                auto abs_rect = capture->GetArrangeRect();
                DrawText(std::format("Name: {},type: {}", capture->Name(), capture->GetType()->Name()), pen, font_size);
                pen.y += line_height;
                DrawText(std::format("Pos: {},Size: {}", Vector2f(abs_rect.xy).ToString(), Vector2f(abs_rect.zw).ToString()), pen, font_size);
                pen.y += line_height;
                auto slot = capture->GetSlot();
                DrawText(std::format("Padding: {},Margin: {}", capture->SlotPadding().ToString(), slot->_margin.ToString()),pen,font_size);
                pen.y += line_height;
                if (auto linear_slot = dynamic_cast<UI::LinearSlot *>(slot.get()))
                {
                    DrawText(std::format("SizePolicyH: {},SizePolicyV: {}", StaticEnum<UI::ESizePolicy>()->GetNameByEnum(linear_slot->_size_policy_h),
                                         StaticEnum<UI::ESizePolicy>()->GetNameByEnum(linear_slot->_size_policy_v)), pen, font_size);
                    pen.y += line_height;
                    DrawText(std::format("CrossAlign: {}", StaticEnum<UI::EAlignment>()->GetNameByEnum(linear_slot->_cross_align)), pen, font_size);
                    pen.y += line_height;
                }
                else if (auto canvas_slot = dynamic_cast<UI::CanvasSlot *>(slot.get()))
                {
                    DrawText(std::format("Position: {}, Anchor: {}", canvas_slot->_position.ToString(), canvas_slot->_anchor.ToString()), pen, font_size);
                    pen.y += line_height;
                    DrawText(std::format("AlighH: {},AlighV: {}", StaticEnum<UI::EAlignment>()->GetNameByEnum(canvas_slot->_alignment_h),
                                         StaticEnum<UI::EAlignment>()->GetNameByEnum(canvas_slot->_alignment_v)), pen, font_size);
                    pen.y += line_height;
                }
                DrawText(std::format("IsFocused: {},IsHover: {},IsPressed: {}", capture->IsFocused(), capture->IsHovered(), capture->IsPressed()), pen, font_size);
                DrawBox(capture->GetArrangeRect().xy, capture->GetArrangeRect().zw, 1.0f, Color(1.0f, 0.5f, 0.0f, 1.0f));
            }
        }
    }// namespace UI
}// namespace Ailu
