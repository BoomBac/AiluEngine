//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_CANVAS_H
#define AILU_CANVAS_H

#include "UIElement.h"
#include "Render/Texture.h"
#include "Objects/Serialize.h"
#include "generated/Widget.gen.h"

namespace Ailu
{
    class Window;
    namespace UI
    {

        ASTRUCT()
        struct SlotItemData
        {
            GENERATED_BODY()
            APROPERTY()
            Ref<UISlot> _slot;
            APROPERTY()
            String _element_id;
            APROPERTY()
            String _element_type;
        };

        /// <summary>
        /// 画布管理其所有ui元素的布局和渲染以及生命周期
        /// </summary>
        ACLASS()
        class AILU_API Widget : public SerializeObject
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_sort_order_changed);
            DECLARE_DELEGATE(on_get_focus);
            DECLARE_DELEGATE(on_lost_focus);
            friend class UIManager;
        public:
            Widget();
            void Destory();
            void Serialize(FArchive &ar) final;
            void Deserialize(FArchive &ar) final;
            void PostDeserialize() final;
            void BindOutput(Render::RenderTexture *color, Render::RenderTexture *depth = nullptr);
            void AddToWidget(Ref<UIElement> root);

            void PreUpdate(f32 dt);
            void Update(f32 dt);
            void PostUpdate(f32 dt);
            void Render(UIRenderer &r);
            void InvalidatePaint(EUIInvalidationReason reason, UIElement *source = nullptr);
            bool IsPaintCacheDirty(u16 frame_index) const;
            void ClearPaintInvalidation(u16 frame_index);
            EUIInvalidationReason GetPaintCacheDirtyReasons() const { return _paint_cache_dirty_reasons; }
            UIElement *GetLastInvalidationSource() const { return _last_invalidation_source; }
            EUIInvalidationReason GetLastInvalidationReasons() const { return _last_invalidation_reasons; }
            u64 GetInvalidationCount() const { return _invalidation_count; }
            bool OnEvent(UIEvent& event);
            bool IsHover(Vector2f pos) const;
            std::tuple<Render::RenderTexture *, Render::RenderTexture *> GetOutput() 
            {
                return _is_external_output ? std::make_tuple(_external_color, _external_depth) : std::make_tuple(_color.get(), _depth.get());
            };
            Vector2f GetSize() const;
            void SetSize(Vector2f size);
            void SetPosition(Vector2f position);
            auto begin() { return _root->begin(); }
            auto end() { return _root->end(); }
            UIElement *Root() { return _root.get(); }
            const Window *Parent() const { return _parent; };
            void SetParent(Window *w) { _parent = w; };
            bool IsPopup() const { return _is_popup; }
            void SetPopup(bool is_popup) { _is_popup = is_popup; }

            bool operator<(const Widget &other) const { return _sort_order < other._sort_order; }
            bool operator>(const Widget &other) const { return _sort_order > other._sort_order; }
        public:
            bool _is_external_output = false;
            APROPERTY()
            u32 _sort_order = 0u;
            APROPERTY()
            bool _is_receive_event = true;
            APROPERTY()
            EVisibility _visibility = EVisibility::kVisible;
        private:
            bool DispatchEvent(UIEvent& e);
            AL_FORCE_INLINE void ResetClickState()
            {
                _last_click_target = nullptr;
                _last_click_button = -1;
                _last_click_pos = {-FLT_MAX, -FLT_MAX};
                _last_click_time = std::chrono::steady_clock::time_point{};
            }
        private:
            APROPERTY()
            Vector2f _size;
            APROPERTY()
            Vector2f _position;
            APROPERTY()
            Vector2f _scale;
            Ref<Render::RenderTexture> _color, _depth;
            Render::RenderTexture *_external_color; 
            Render::RenderTexture * _external_depth;
            Ref<UIElement> _root;
            EUIInvalidationReason _paint_cache_dirty_reasons = EUIInvalidationReason::kPaint;
            UIElement *_last_invalidation_source = nullptr;
            EUIInvalidationReason _last_invalidation_reasons = EUIInvalidationReason::kNone;
            u64 _invalidation_count = 0u;
            Array<bool, Render::RenderConstants::kFrameCount> _is_paint_cache_frame_valid = {};
            Vector<UIElement*> _prev_hover_path;
            bool _is_popup = false;
            // 双击检测状态
            std::chrono::steady_clock::time_point _last_click_time{};
            Vector2f _last_click_pos{-FLT_MAX, -FLT_MAX};
            UIElement *_last_click_target{nullptr};
            i32 _last_click_button{-1};
            // 配置阈值
            i32 _double_click_time_ms = 400;    // 时间阈值(ms)
            f32 _double_click_max_dist = 4.0f;// 位置阈值(像素)
            Window *_parent;
        };

    }// namespace UI
}// namespace Ailu

#endif//AILU_CANVAS_H
