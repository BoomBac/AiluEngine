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
#include "Render/Material.h"
#include <Framework/Common/Allocator.hpp>
#include <Framework/Common/ResourceMgr.h>
#include "Render/Shader.h"
#include "Render/GraphicsContext.h"
#include <chrono>
#include <cmath>

namespace Ailu
{
    namespace UI
    {
        using namespace Render;
        static UIRenderer *s_Renderer = nullptr;
        namespace
        {
            constexpr u16 kBackdropBlurDownsample = 2u;
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
            _obj_cb = ConstantBuffer::Create(Render::RenderConstants::kPerObjectDataSize);
            _default_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_ui.alasset"), "DefaultUIMaterial");
            _default_material->SetTexture("_MainTex", Render::Texture::s_p_default_white);
            _shadow_material = MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/ui_shadow.alasset"), "UIShadowMaterial");
            _shadow_material->SetFloat("_ShadowSpread", 16.0f);
            _backdrop_blur_cs = ResourceMgr::Get().LoadAssetRef<ComputeShader>(L"Shaders/hlsl/Compute/blur.alasset");
            _backdrop_blur_x_kernel = _backdrop_blur_cs->FindKernel("blur_x");
            _backdrop_blur_y_kernel = _backdrop_blur_cs->FindKernel("blur_y");
            for (auto &frame_blocks: _drawer_blocks)
            {
                frame_blocks.push_back(AL_NEW(DrawerBlock, _default_material,9600u));
            }
            _text_block = AL_NEW(DrawerBlock,MakeRef<Material>(ResourceMgr::Get().Get<Shader>(L"Shaders/hlsl/default_text.alasset"), "DefaultTextMaterial"));
            for (auto &frame_blocks: _popup_backdrop_blocks)
                frame_blocks.push_back(AL_NEW(DrawerBlock, _default_material, 8u));
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
                AL_DELETE(entry._texture_cache_quad);
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
            for (auto &frame_blocks: _popup_backdrop_blocks)
            {
                for (auto *block: frame_blocks)
                    AL_DELETE(block);
            }
        }

        void UIRenderer::Render(CommandBuffer *cmd)
        {
            Prepare();
            Record(cmd);
        }

        void UIRenderer::Prepare()
        {
            PROFILE_BLOCK_CPU("UIRenderer::Prepare")
            // NewFrame has waited for this slot's GPU work before any mapped UI buffers are rewritten.
            _frame_index = static_cast<u16>(FrameResourceManager::Get().GetActiveFrameSlot());
            ResetFrameStats();
            const bool collect_widget_draw_stats = Profiler::Get().IsRecording();
            _cur_global_block_index = 0u;
            for (auto *block: _drawer_blocks[_frame_index])
            {
                if (block != nullptr)
                    block->ResetBuildData();
            }
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
                PROFILE_BLOCK_CPU("UIRenderer::VisibleWidgetCollect")
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
                AL_DELETE(it->_texture_cache_quad);
                it = _widget_drawer_blocks.erase(it);
            }
            _widget_drawer_blocks.reserve(_widget_drawer_blocks.size() + visible_widgets.size());
            const bool is_debug_paint_dirty_visible = UIManager::Get()->IsDebugPaintDirtyVisible();
            if (is_debug_paint_dirty_visible)
            {
                for (auto *canvas: visible_widgets)
                {
                    if (canvas->Root() != nullptr)
                        canvas->Root()->ClearDebugPaintDirtyRecursive();
                }
            }
            Vector<Widget *> updated_widgets;
            Vector<Widget *> widgets_to_update;
            updated_widgets.reserve(visible_widgets.size());
            widgets_to_update.reserve(visible_widgets.size());
            for (auto *canvas: visible_widgets)
            {
                updated_widgets.push_back(canvas);
                if (!is_debug_paint_dirty_visible && !canvas->NeedsUpdate())
                {
                    ++_stats._ui_widget_update_skipped_count;
                    continue;
                }
                canvas->_is_update_dirty = false;
                ++_stats._ui_widget_update_executed_count;
                PROFILE_BLOCK_CPU("UIRenderer::WidgetPreUpdate")
                canvas->PreUpdate(dt);
                widgets_to_update.push_back(canvas);
            }
            for (auto *canvas: widgets_to_update)
            {
                PROFILE_BLOCK_CPU("UIRenderer::WidgetUpdate")
                canvas->Update(dt);
            }
            UI::UIManager::Get()->EnsurePopupWidgetsOnTop();
            visible_widgets.clear();
            visible_widgets.reserve(widgets.size());
            for (auto &widget: widgets)
            {
                PROFILE_BLOCK_CPU("UIRenderer::VisibleWidgetCollect")
                auto *canvas = widget.get();
                if (canvas->_visibility == EVisibility::kVisible)
                    visible_widgets.push_back(canvas);
            }
            for (auto *canvas: visible_widgets)
            {
                PROFILE_BLOCK_CPU("UIRenderer::LateWidgetUpdate")
                if (std::find(updated_widgets.begin(), updated_widgets.end(), canvas) != updated_widgets.end())
                    continue;
                updated_widgets.push_back(canvas);
                if (!is_debug_paint_dirty_visible && !canvas->NeedsUpdate())
                {
                    ++_stats._ui_widget_update_skipped_count;
                    continue;
                }
                if (is_debug_paint_dirty_visible && canvas->Root() != nullptr)
                    canvas->Root()->ClearDebugPaintDirtyRecursive();
                canvas->_is_update_dirty = false;
                ++_stats._ui_widget_update_executed_count;
                canvas->PreUpdate(dt);
                canvas->Update(dt);
            }
            DragDropManager::Get().Update();
            _submit_entries.clear();
            _submit_entries.reserve(visible_widgets.size());
            _widget_draw_stats.clear();
            if (collect_widget_draw_stats)
                _widget_draw_stats.reserve(visible_widgets.size());
            _cur_widget_index = 1u;
            for (auto *canvas: visible_widgets)
            {
                auto *entry = GetWidgetBlockEntry(canvas);
                auto &cpu_blocks = entry->_cpu_blocks;
                _cur_widget_blocks = &cpu_blocks;
                _cur_widget_block_index = 0u;
                _cur_widget_block = cpu_blocks.empty() ? nullptr : cpu_blocks[0u];

                if (canvas->IsPaintCacheDirty(_frame_index) || !entry->_cache_built)
                {
                    PROFILE_BLOCK_CPU("UIRenderer::WidgetPaintBuild")
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
                    if (is_debug_paint_dirty_visible && canvas->Root() != nullptr)
                        canvas->Root()->SnapshotPaintDirtyToDebugRecursive();
                    ++entry->_build_revision;
                    entry->_cache_built = !_cache_build_pending_resource;
                    entry->_gpu_revisions.fill(0u);
                    if (!_cache_build_pending_resource)
                    {
                        for (u16 frame_index = 0u; frame_index < entry->_gpu_revisions.size(); ++frame_index)
                            canvas->ClearPaintInvalidation(frame_index);
                    }
                    if (is_debug_paint_dirty_visible)
                        DrawDirtyStateOverlay(canvas->Root());
                    ++_stats._ui_cache_miss_count;
                }
                else
                {
                    if (is_debug_paint_dirty_visible)
                        DrawDirtyStateOverlay(canvas->Root());
                    ++_stats._ui_cache_hit_count;
                }
                PROFILE_BLOCK_CPU("UIRenderer::WidgetSync")
                SyncWidgetFrameBlocks(canvas);
                auto &blocks = GetWidgetFrameBlocks(canvas);
                bool has_nodes = false;
                UIWidgetDrawStats widget_draw_stats;
                if (collect_widget_draw_stats)
                    widget_draw_stats._widget_name = canvas->Name();
                for (auto *block: blocks)
                {
                    if (block == nullptr)
                        continue;
                    if (collect_widget_draw_stats)
                        widget_draw_stats._draw_node_count += block->_nodes.size();
                    has_nodes = has_nodes || !block->_nodes.empty();
                }
                if (collect_widget_draw_stats)
                    _widget_draw_stats.push_back(std::move(widget_draw_stats));
                if (!has_nodes)
                {
                    canvas->_texture_cache.reset();
                    entry->_texture_cache_valid = false;
                    if (entry->_texture_cache_quad != nullptr)
                    {
                        AL_DELETE(entry->_texture_cache_quad);
                        entry->_texture_cache_quad = nullptr;
                    }
                    ++_cur_widget_index;
                    continue;
                }

                const bool use_cache_materials = canvas->_texture_cache_mode == EWidgetTextureCacheMode::kTexture;
                const bool is_texture_cache_requested = use_cache_materials ||
                    canvas->_texture_cache_mode == EWidgetTextureCacheMode::kOpaqueTexture;
                // Enclose fractional Dock bounds in whole pixels so cache/composite sample the same pixel centers.
                const Vector2f cache_origin{std::floor(canvas->_position.x), std::floor(canvas->_position.y)};
                const Vector2f widget_size = canvas->GetSize();
                const Vector2f cache_size{std::ceil(canvas->_position.x + widget_size.x) - cache_origin.x,
                    std::ceil(canvas->_position.y + widget_size.y) - cache_origin.y};
                bool is_texture_cache_supported = is_texture_cache_requested && !canvas->IsPopup() &&
                    canvas->_is_external_output && canvas->_external_color != nullptr && canvas->_external_color->IsSwapChain() &&
                    canvas->_external_depth == nullptr && entry->_cache_built && !is_debug_paint_dirty_visible &&
                    std::isfinite(cache_origin.x) && std::isfinite(cache_origin.y);
                DrawerBlock *first_block = nullptr;
                u64 image_upload_revision = 0u;
                if (is_texture_cache_supported)
                    for (auto *block: blocks)
                    {
                        if (block == nullptr || block->_nodes.empty())
                            continue;
                        if (first_block == nullptr)
                            first_block = block;
                        // Ordinary images and static preview RTs require an audited owner revision contract.
                        for (const auto &node: block->_nodes)
                        {
                            if (node._main_tex != nullptr)
                                image_upload_revision = image_upload_revision * 1099511628211ull + node._main_tex->UploadRevision();
                            if (node._is_backdrop_blur ||
                                (dynamic_cast<RenderTexture *>(node._main_tex) != nullptr && !canvas->_has_texture_cache_image_revision) ||
                                (node._main_tex != nullptr && !node._main_tex->IsReady()) ||
                                (node._mat != _default_material.get() && node._mat->GetActiveShader() != _text_block->_mat->GetActiveShader()) ||
                                (node._mat == _default_material.get() && node._main_tex != nullptr &&
                                    node._main_tex != Texture::s_p_default_white && !canvas->_has_texture_cache_image_revision))
                                is_texture_cache_supported = false;
                        }
                    }
                if (entry->_texture_cache_upload_revision != image_upload_revision)
                {
                    canvas->_texture_cache_dirty = true;
                    entry->_texture_cache_upload_revision = image_upload_revision;
                }
                // Retain the original opaque toolbar trial; all expanded candidates require alpha composition.
                is_texture_cache_supported = is_texture_cache_supported && first_block != nullptr &&
                    (use_cache_materials || (canvas->Name() == "EditorToolbar" && cache_origin.x == 0.0f && cache_origin.y == 0.0f &&
                        canvas->_external_color == RenderTexture::s_backbuffer));
                // Geometry may change during fallback; never resume with the previous RT contents.
                if (!is_texture_cache_supported)
                    entry->_texture_cache_valid = false;
                if (canvas->_texture_cache_mode == EWidgetTextureCacheMode::kGeometry)
                {
                    canvas->_texture_cache.reset();
                    entry->_texture_cache_valid = false;
                    if (entry->_texture_cache_quad != nullptr)
                    {
                        AL_DELETE(entry->_texture_cache_quad);
                        entry->_texture_cache_quad = nullptr;
                    }
                }
                WidgetSubmitEntry submit_entry;
                submit_entry._widget = canvas;
                auto [color, depth] = canvas->GetOutput();
                submit_entry._color = color;
                submit_entry._depth = depth;
                if (is_texture_cache_supported && !use_cache_materials && _texture_cache_composite_material == nullptr)
                {
                    auto cache_shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/ui_cache_opaque.alasset");
                    if (cache_shader != nullptr)
                        _texture_cache_composite_material = MakeRef<Material>(cache_shader.get(), "UITextureCacheOpaque");
                }
                if (is_texture_cache_supported && use_cache_materials && _texture_cache_alpha_composite_material == nullptr)
                {
                    auto composite_shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/ui_cache_alpha.alasset");
                    auto ui_shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/ui_cache_draw.alasset");
                    auto text_shader = ResourceMgr::Get().Load<Shader>(L"Shaders/hlsl/ui_cache_text.alasset");
                    if (composite_shader != nullptr && ui_shader != nullptr && text_shader != nullptr)
                    {
                        _texture_cache_alpha_composite_material = MakeRef<Material>(composite_shader.get(), "UITextureCacheAlpha");
                        _texture_cache_ui_material = MakeRef<Material>(ui_shader.get(), "UITextureCacheDraw");
                        _texture_cache_ui_material->SetVector("_Color", Colors::kWhite);
                        _texture_cache_text_material = MakeRef<Material>(text_shader.get(), "UITextureCacheText");
                        _texture_cache_msdf_material = MakeRef<Material>(text_shader.get(), "UITextureCacheMsdf");
                        _texture_cache_msdf_material->EnableKeyword("_MSDF");
                    }
                }
                auto cache_composite_material = use_cache_materials ? _texture_cache_alpha_composite_material : _texture_cache_composite_material;
                if (is_texture_cache_supported && cache_composite_material != nullptr && cache_composite_material->IsReadyForDraw() &&
                    (!use_cache_materials || (_texture_cache_ui_material->IsReadyForDraw() &&
                        _texture_cache_text_material->IsReadyForDraw() && _texture_cache_msdf_material->IsReadyForDraw())))
                {
                    const bool is_cache_size_valid = std::isfinite(widget_size.x) && std::isfinite(widget_size.y) &&
                        widget_size.x > 0.0f && widget_size.y > 0.0f && std::isfinite(cache_size.x) && std::isfinite(cache_size.y) &&
                        cache_size.x >= 1.0f && cache_size.y >= 1.0f && cache_size.x <= 4095.0f && cache_size.y <= 4095.0f;
                    const u16 cache_width = is_cache_size_valid ? static_cast<u16>(cache_size.x) : 0u;
                    const u16 cache_height = is_cache_size_valid ? static_cast<u16>(cache_size.y) : 0u;
                    if (cache_width > 0u && cache_height > 0u)
                    {
                        if (canvas->_texture_cache == nullptr || canvas->_texture_cache->Width() != cache_width ||
                            canvas->_texture_cache->Height() != cache_height)
                        {
                            TextureDesc cache_desc(cache_width, cache_height, ERenderTargetFormat::kDefault);
                            cache_desc._clear_color = Colors::kTransparent;
                            cache_desc._load = ELoadStoreAction::kLoad;
                            canvas->_texture_cache = RenderTexture::Create(cache_desc, std::format("UIWidgetCache_{}", canvas->Name()));
                            if (canvas->_texture_cache != nullptr)
                            {
                                canvas->_texture_cache->_load_action = ELoadStoreAction::kLoad;
                                canvas->_texture_cache_dirty = true;
                                entry->_texture_cache_valid = false;
                            }
                        }
                        if (canvas->_texture_cache != nullptr)
                        {
                            const Vector4f cache_rect{cache_origin.x, cache_origin.y, cache_size.x, cache_size.y};
                            if (entry->_texture_cache_quad == nullptr)
                                entry->_texture_cache_quad = AL_NEW(DrawerBlock, cache_composite_material, 6u);
                            auto *cache_quad = entry->_texture_cache_quad;
                            const bool needs_quad_build = cache_quad->_mat != cache_composite_material || cache_quad->_nodes.empty() ||
                                cache_quad->_nodes[0]._main_tex != canvas->_texture_cache.get() ||
                                entry->_texture_cache_quad_rect.x != cache_rect.x ||
                                entry->_texture_cache_quad_rect.y != cache_rect.y ||
                                entry->_texture_cache_quad_rect.z != cache_rect.z ||
                                entry->_texture_cache_quad_rect.w != cache_rect.w;
                            if (needs_quad_build)
                            {
                                cache_quad->_mat = cache_composite_material;
                                cache_quad->ResetBuildData();
                                const Vector3f positions[4] = {{cache_rect.x, cache_rect.y, 0.0f},
                                    {cache_rect.x + cache_rect.z, cache_rect.y, 0.0f},
                                    {cache_rect.x, cache_rect.y + cache_rect.w, 0.0f},
                                    {cache_rect.x + cache_rect.z, cache_rect.y + cache_rect.w, 0.0f}};
                                const Vector2f uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
                                for (u32 vertex_index = 0u; vertex_index < 4u; ++vertex_index)
                                {
                                    cache_quad->_pos_buf[vertex_index] = positions[vertex_index];
                                    cache_quad->_uv_buf[vertex_index] = uvs[vertex_index];
                                    cache_quad->_color_buf[vertex_index] = Colors::kWhite;
                                    cache_quad->_rect_buf[vertex_index] = cache_rect;
                                    cache_quad->_corner_radius_buf[vertex_index] = Vector4f::kZero;
                                    cache_quad->_border_thickness_buf[vertex_index] = Vector4f::kZero;
                                }
                                cache_quad->_index_buf[0] = 0u;
                                cache_quad->_index_buf[1] = 1u;
                                cache_quad->_index_buf[2] = 2u;
                                cache_quad->_index_buf[3] = 1u;
                                cache_quad->_index_buf[4] = 3u;
                                cache_quad->_index_buf[5] = 2u;
                                cache_quad->AppendNode(4u, 6u, cache_composite_material.get(),
                                                       canvas->_texture_cache.get());
                                entry->_texture_cache_quad_rect = cache_rect;
                            }
                            submit_entry._use_texture_cache = true;
                            submit_entry._texture_cache_needs_repaint =
                                canvas->_texture_cache_dirty || !entry->_texture_cache_valid || needs_quad_build;
                            canvas->_texture_cache_dirty = false;
                            submit_entry._use_cache_materials = use_cache_materials;
                            submit_entry._texture_cache_origin = cache_origin;
                            submit_entry._texture_cache = canvas->_texture_cache;
                            submit_entry._texture_cache_quad = entry->_texture_cache_quad;
                            submit_entry._texture_cache_state = entry;
                            _stats._ui_texture_cache_resident_bytes += static_cast<u64>(cache_width) * cache_height * 4u;
                            if (submit_entry._texture_cache_needs_repaint)
                            {
                                ++_stats._ui_texture_cache_miss_count;
                                ++_stats._ui_texture_cache_dirty_count;
                            }
                            else
                                ++_stats._ui_texture_cache_hit_count;
                        }
                    }
                }
                _submit_entries.push_back(std::move(submit_entry));
                ++_cur_widget_index;
            }
            _cur_widget_block = nullptr;
            _cur_widget_blocks = nullptr;
            _cur_widget_block_index = 0u;
            _cur_widget_index = 0u;
            if (_overlay_draw_callback)
                _overlay_draw_callback();
        }

        void UIRenderer::Record(CommandBuffer *cmd)
        {
            PROFILE_BLOCK_CPU("UIRenderer::Record")
            HashMap<RenderTexture *, RTHandle> composite_target_handles;
            auto get_submit_color = [&](RenderTexture *color) -> RenderTexture *
            {
                if (color == nullptr || !color->IsSwapChain())
                    return color;
                if (auto it = composite_target_handles.find(color); it != composite_target_handles.end())
                    return g_pRenderTexturePool->Get(it->second);
                // The composite target stores LDR color, so it must be an sRGB format: the hardware
                // then encodes on write and decodes on sample, which keeps the values (and therefore
                // the final image) identical to a linear target while quantizing perceptually
                // uniformly.  An 8bit linear target quantizes at 1/255 of the linear range, which
                // turns dark gradients - and the whole editor UI composited here - into visible
                // banding.
                const RTHandle handle = cmd->GetTempRT(color->Width(), color->Height(),
                                                       std::format("UI_Composite_{}", composite_target_handles.size()),
                                                       ERenderTargetFormat::kDefault, false, false, false);
                _pending_backdrop_blur_release_handles.push_back(handle);
                composite_target_handles[color] = handle;
                auto *composite = g_pRenderTexturePool->Get(handle);
                cmd->SetRenderTarget(composite);
                cmd->ClearRenderTarget(Colors::kBlack);
                cmd->SetRenderTargetLoadAction(composite, ELoadStoreAction::kLoad);
                return composite;
            };
            for (const auto &entry: _submit_entries)
            {
                if (!entry._use_texture_cache || !entry._texture_cache_needs_repaint || entry._texture_cache == nullptr)
                    continue;
                const auto repaint_start = UIClock::now();
                cmd->SetRenderTarget(entry._texture_cache.get(), nullptr);
                cmd->ClearRenderTarget(Color(0.0f, 0.0f, 0.0f, 0.0f));
                bool is_repaint_complete = true;
                for (auto *block: GetWidgetFrameBlocks(entry._widget))
                {
                    if (block == nullptr || block->_nodes.empty())
                        continue;
                    is_repaint_complete = is_repaint_complete && block->IsReady();
                    for (const auto &node: block->_nodes)
                        is_repaint_complete = is_repaint_complete && node._mat->IsReadyForDraw();
                    SubmitBlock(block, cmd, entry._texture_cache.get(), nullptr, entry._texture_cache_origin, entry._use_cache_materials);
                }
                if (entry._texture_cache_state != nullptr)
                    entry._texture_cache_state->_texture_cache_valid = is_repaint_complete;
                _stats._ui_texture_cache_repaint_time += ElapsedMs(repaint_start);
            }

            for (const auto &entry: _submit_entries)
            {
                if (entry._widget == nullptr || entry._widget->IsPopup())
                    continue;
                auto *submit_color = get_submit_color(entry._color);
                if (entry._use_texture_cache && entry._texture_cache_quad != nullptr)
                {
                    SubmitBlock(entry._texture_cache_quad, cmd, submit_color, entry._depth);
                    ++_stats._ui_texture_cache_composite_draw_count;
                }
                else
                {
                    for (auto *block: GetWidgetFrameBlocks(entry._widget))
                        SubmitBlock(block, cmd, submit_color, entry._depth);
                }
            }
            //绘制全局gui
            for (auto *block: _drawer_blocks[_frame_index])
                SubmitBlock(block, cmd, get_submit_color(RenderTexture::s_backbuffer));
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
            _popup_backdrop_block_index = 0u;
            for (const auto &entry: _submit_entries)
            {
                if (entry._widget == nullptr || !entry._widget->IsPopup())
                    continue;
                auto *submit_color = get_submit_color(entry._color);
                if (UIManager::Get()->ShouldRenderPopupBackdrop(entry._widget))
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
            if (cb == nullptr || !cb->CanAppend(4u, 6u))
            {
                AL_ASSERT_MSG(false, "UI DrawerBlock capacity overflow!");
                return;
            }
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
            const Vector3f &top_left = cb->_pos_buf[cur_vert_num];
            const Vector3f &top_right = cb->_pos_buf[cur_vert_num + 1];
            const Vector3f &bottom_left = cb->_pos_buf[cur_vert_num + 2];
            const Vector4f transformed_rect = {top_left.x, top_left.y,
                                               std::abs(top_right.x - top_left.x),
                                               std::abs(bottom_left.y - top_left.y)};
            cb->_uv_buf[cur_vert_num] = brush._uv_rect.xy;
            cb->_uv_buf[cur_vert_num + 1] = {brush._uv_rect.x + brush._uv_rect.z, brush._uv_rect.y};
            cb->_uv_buf[cur_vert_num + 2] = {brush._uv_rect.x, brush._uv_rect.y + brush._uv_rect.w};
            cb->_uv_buf[cur_vert_num + 3] = {brush._uv_rect.x + brush._uv_rect.z, brush._uv_rect.y + brush._uv_rect.w};
            cb->_color_buf[cur_vert_num] = color;
            cb->_color_buf[cur_vert_num + 1] = color;
            cb->_color_buf[cur_vert_num + 2] = color;
            cb->_color_buf[cur_vert_num + 3] = color;
            cb->_rect_buf[cur_vert_num] = transformed_rect;
            cb->_rect_buf[cur_vert_num + 1] = transformed_rect;
            cb->_rect_buf[cur_vert_num + 2] = transformed_rect;
            cb->_rect_buf[cur_vert_num + 3] = transformed_rect;
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

        UIBrush UIRenderer::ResolveBackdropBrush(const UIElement *element, const UIBrush &brush) const
        {
            if (element == nullptr || brush._type != EUIBrushType::kBackdropBlur)
                return brush;
            if (brush._texture != nullptr)
                return brush;
            Widget *widget = element->GetOwningWidget();
            if (widget == nullptr)
                return brush;
            const auto output = widget->GetOutput();
            auto *color = std::get<0>(output);
            if (color == nullptr || color->Width() == 0u || color->Height() == 0u)
                return brush;

            const f32 width = static_cast<f32>(color->Width());
            const f32 height = static_cast<f32>(color->Height());
            const Vector4f rect = element->GetArrangeRect();
            UIBrush resolved_brush = brush;
            resolved_brush._texture = color;
            resolved_brush._uv_rect = {rect.x / width, rect.y / height, rect.z / width, rect.w / height};
            return resolved_brush;
        }

        void UIRenderer::DrawVisual(Vector4f rect, Matrix4x4f matrix, const UIControlVisual &visual,
                                    const UIElement *element)
        {
            if (visual._background._type != EUIBrushType::kNone && visual._background._tint.a > 0.0f)
            {
                const UIBrush background = ResolveBackdropBrush(element, visual._background);
                DrawQuad(rect, matrix, background, visual._corner_radius);
            }
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

        void UIRenderer::DrawImage(Render::Texture *texture, Vector4f rect, const ImageDrawOptions &opts,
                                   Render::Material *material)
        {
            if (!texture)
                return;
            DrawerBlock *cb = GetAvailableBlock(4u, 6u);
            if (cb == nullptr)
                return;
            u32 cur_vert_num = cb->CurrentVertNum(), cur_index_num = cb->CurrentIndexNum();
            cb->_pos_buf[cur_vert_num] = {rect.xy, opts._depth};
            cb->_pos_buf[cur_vert_num + 1] = {rect.x + rect.z, rect.y, opts._depth};
            cb->_pos_buf[cur_vert_num + 2] = {rect.x, rect.y + rect.w, opts._depth};
            cb->_pos_buf[cur_vert_num + 3] = {rect.x + rect.z, rect.y + rect.w, opts._depth};
            TransformCoord(cb->_pos_buf[cur_vert_num], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 1], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 2], opts._transform);
            TransformCoord(cb->_pos_buf[cur_vert_num + 3], opts._transform);
            const Vector3f &top_left = cb->_pos_buf[cur_vert_num];
            const Vector3f &top_right = cb->_pos_buf[cur_vert_num + 1];
            const Vector3f &bottom_left = cb->_pos_buf[cur_vert_num + 2];
            const Vector4f transformed_rect = {top_left.x, top_left.y,
                                               std::abs(top_right.x - top_left.x),
                                               std::abs(bottom_left.y - top_left.y)};
            const Vector4f uv_rect = opts._uv_rect;
            cb->_uv_buf[cur_vert_num] = {uv_rect.x, uv_rect.y};
            cb->_uv_buf[cur_vert_num + 1] = {uv_rect.x + uv_rect.z, uv_rect.y};
            cb->_uv_buf[cur_vert_num + 2] = {uv_rect.x, uv_rect.y + uv_rect.w};
            cb->_uv_buf[cur_vert_num + 3] = {uv_rect.x + uv_rect.z, uv_rect.y + uv_rect.w};
            cb->_color_buf[cur_vert_num]     = opts._tint;
            cb->_color_buf[cur_vert_num + 1] = opts._tint;
            cb->_color_buf[cur_vert_num + 2] = opts._tint;
            cb->_color_buf[cur_vert_num + 3] = opts._tint;
            cb->_rect_buf[cur_vert_num] = transformed_rect;
            cb->_rect_buf[cur_vert_num + 1] = transformed_rect;
            cb->_rect_buf[cur_vert_num + 2] = transformed_rect;
            cb->_rect_buf[cur_vert_num + 3] = transformed_rect;
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
            AppendNode(cb, 4u, 6u, material != nullptr ? material : _default_material.get(), texture);
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
            if (cb == nullptr)
                return;
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
            if (entry == nullptr || _frame_index >= entry->_blocks.size() ||
                entry->_gpu_revisions[_frame_index] == entry->_build_revision)
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
                    if ((*_cur_widget_blocks)[_cur_widget_block_index] == nullptr)
                        (*_cur_widget_blocks)[_cur_widget_block_index] = AL_NEW(DrawerBlock, _default_material, 8092u * 4);
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
                return _cur_widget_block->CanAppend(vert_num, index_num) ? _cur_widget_block : nullptr;
            }
            auto &frame_block = _drawer_blocks[_frame_index];
            while (frame_block.size() <= _cur_global_block_index)
            {
                frame_block.push_back(AL_NEW(DrawerBlock, _default_material,8092u * 4));
            }
            available_block = frame_block[_cur_global_block_index];
            if (available_block == nullptr)
            {
                available_block = AL_NEW(DrawerBlock, _default_material, 8092u * 4);
                frame_block[_cur_global_block_index] = available_block;
            }
            if (!available_block->CanAppend(vert_num, index_num))
            {
                ++_cur_global_block_index;
                while (frame_block.size() <= _cur_global_block_index)
                    frame_block.push_back(AL_NEW(DrawerBlock, _default_material, 8092u * 4));
                available_block = frame_block[_cur_global_block_index];
            }
            AL_ASSERT_MSG(available_block != nullptr && available_block->CanAppend(vert_num, index_num),
                          "UI DrawerBlock index overflow!");
            return available_block != nullptr && available_block->CanAppend(vert_num, index_num) ? available_block : nullptr;
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

            cmd->RequireState(downsample_rt, EResourceState::kRenderTarget);
            cmd->Blit(source, downsample);
            cmd->RequireState(downsample_rt, EResourceState::kNonPixelShaderResource);
            cmd->RequireState(blur_x_rt, EResourceState::kUnorderedAccess);
            _backdrop_blur_cs->SetTexture(_backdrop_blur_x_kernel, "_BlurSource", downsample);
            _backdrop_blur_cs->SetTexture(_backdrop_blur_x_kernel, "_BlurResult", blur_x);
            auto [group_num_x, group_num_y, group_num_z] = _backdrop_blur_cs->CalculateDispatchNum(_backdrop_blur_x_kernel, blur_width, blur_height, 1u);
            cmd->Dispatch(_backdrop_blur_cs.get(), _backdrop_blur_x_kernel, group_num_x, group_num_y, 1u);
            // The following transition out of kUnorderedAccess already orders the dispatch writes; an explicit
            // UAV barrier before it is redundant.
            cmd->RequireState(blur_x_rt, EResourceState::kNonPixelShaderResource);
            cmd->RequireState(blur_y_rt, EResourceState::kUnorderedAccess);
            _backdrop_blur_cs->SetTexture(_backdrop_blur_y_kernel, "_BlurSource", blur_x);
            _backdrop_blur_cs->SetTexture(_backdrop_blur_y_kernel, "_BlurResult", blur_y);
            cmd->Dispatch(_backdrop_blur_cs.get(), _backdrop_blur_y_kernel, group_num_x, group_num_y, 1u);
            cmd->RequireState(blur_y_rt, EResourceState::kPixelShaderResource);

            _frame_backdrop_blur_cache[source] = blur_y_rt;
            return blur_y_rt;
        }

        void UIRenderer::SubmitPopupBackdrop(Widget *widget, CommandBuffer *cmd, RenderTexture *color, RenderTexture *depth)
        {
            if (widget == nullptr || widget->Root() == nullptr || color == nullptr)
                return;
            const f32 w = static_cast<f32>(color->Width());
            const f32 h = static_cast<f32>(color->Height());
            const bool is_modal = UIManager::Get() != nullptr && UIManager::Get()->IsPopupModal(widget);
            const Vector4f rect = is_modal ? Vector4f(0.0f, 0.0f, w, h) : widget->Root()->GetArrangeRect();
            if (rect.z <= 1.0f || rect.w <= 1.0f)
                return;

            auto &backdrop_blocks = _popup_backdrop_blocks[_frame_index];
            if (_popup_backdrop_block_index >= backdrop_blocks.size())
                backdrop_blocks.push_back(AL_NEW(DrawerBlock, _default_material, 8u));
            DrawerBlock *backdrop_block = backdrop_blocks[_popup_backdrop_block_index++];
            backdrop_block->ResetBuildData();
            _frame_backdrop_blur_cache.erase(color);

            UIBrush brush;
            brush._type = EUIBrushType::kBackdropBlur;
            brush._texture = color;
            brush._tint = Color(1.0f, 1.0f, 1.0f, 0.92f);
            brush._uv_rect = {rect.x / w, rect.y / h, rect.z / w, rect.w / h};
            AppendQuadToBlock(backdrop_block, rect, kIdentityMatrix, brush,
                              is_modal ? Vector4f(0.0f) : Vector4f(10.0f), 0.0f);
            SubmitBlock(backdrop_block, cmd, color, depth);
        }

        void UIRenderer::SubmitBlock(DrawerBlock *b, CommandBuffer *cmd,RenderTexture* color,RenderTexture* depth, Vector2f origin, bool use_cache_materials)
        {
            PROFILE_BLOCK_CPU("UIRenderer::SubmitBlock")
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
            BuildOrthographicMatrix(proj, origin.x, origin.x + w, origin.y, origin.y + h, 1.f, 200.f);
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
                Material *material = node._mat;
                if (use_cache_materials)
                    material = node._mat == _default_material.get() ? _texture_cache_ui_material.get() :
                        (node._msdf_px_range > 0.0f ? _texture_cache_msdf_material.get() : _texture_cache_text_material.get());
                material->SetTexture("_MainTex", main_tex ? main_tex : Texture::s_p_default_white);
                material->SetFloat("_MsdfPxRange", node._msdf_px_range);
                Rect scissor = full_rect;
                if (node._is_custom_scissor)
                {
                    const f32 left = std::clamp(static_cast<f32>(node._scissor.left) - origin.x, 0.0f, w);
                    const f32 top = std::clamp(static_cast<f32>(node._scissor.top) - origin.y, 0.0f, h);
                    // UI scissors use (left, top, right, bottom), matching the DX12 conversion, despite the Rect field names.
                    const f32 right = std::clamp(static_cast<f32>(node._scissor.width) - origin.x, left, w);
                    const f32 bottom = std::clamp(static_cast<f32>(node._scissor.height) - origin.y, top, h);
                    if (right <= left || bottom <= top)
                        continue;
                    scissor = Rect(static_cast<u16>(left), static_cast<u16>(top), static_cast<u16>(right), static_cast<u16>(bottom));
                }
                if (prev_scissor != scissor)
                    cmd->SetScissorRect(scissor);
                cmd->DrawIndexed(b->_vbuf.get(), b->_ibuf.get(), _obj_cb.get(), material,0u,node._index_offset,node._index_num);
                ++_stats._ui_draw_call_count;
                prev_scissor = scissor;
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
