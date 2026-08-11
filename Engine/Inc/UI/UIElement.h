//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIELEMENT_H
#define AILU_UIELEMENT_H

#include "Framework/Math/Transform2D.h"
#include "Framework/Core/Delegate.h"
#include "Objects/Serialize.h"
#include <Render/RendererAPI.h>
#include "Framework/Common/Allocator.hpp"
#include "DragDrop.h"
#include "UISlot.h"
#include "generated/UIElement.gen.h"

namespace Ailu
{
    namespace UI
    {
        AENUM()
        enum class EVisibility
        {
            kVisible,
            kHide
        };

        class UIElement;
        class UIRenderer;
        struct AILU_API UIEvent
        {
            enum class EType
            {
                kMouseEnter,
                kMouseExit,
                kMouseDown,
                kMouseUp,
                kMouseDoubleClick,
                kMouseClick,
                kMouseScroll,
                kMouseMove,
                kMouseExitWindow,
                kKeyDown,
                kKeyUp,
                kDropFiles
            };

            EType _type;
            Vector2f _mouse_position;
            Vector2f _mouse_delta;
            UIElement *_target = nullptr;
            UIElement *_current_target = nullptr;// 用于冒泡阶段
            u32 _key_code = 0u;                  // 按键事件使用
            bool _is_handled = false;
            f32 _scroll_delta = 0.0f;// 滚轮滚动增量
            Vector<WString> _drop_files;

            bool operator==(const UIEvent &other) const
            {
                return _type == other._type &&
                       _mouse_position == other._mouse_position &&
                       _target == other._target &&
                       _current_target == other._current_target && 
                    _key_code == other._key_code &&
                    _is_handled == other._is_handled &&
                       _scroll_delta == other._scroll_delta;
            }

            String ToString() const;
        private:
            static std::string TypeToString(EType t)
            {
                switch (t)
                {
                    case EType::kMouseEnter:
                        return "MouseEnter";
                    case EType::kMouseExit:
                        return "MouseExit";
                    case EType::kMouseDown:
                        return "MouseDown";
                    case EType::kMouseUp:
                        return "MouseUp";
                    case EType::kMouseDoubleClick:
                        return "MouseDoubleClick";
                    case EType::kMouseClick:
                        return "MouseClick";
                    case EType::kMouseScroll:
                        return "MouseScroll";
                    case EType::kMouseMove:
                        return "MouseMove";
                    case EType::kMouseExitWindow:
                        return "MouseExitWindow";
                    case EType::kKeyDown:
                        return "KeyDown";
                    case EType::kKeyUp:
                        return "KeyUp";
                    case EType::kDropFiles:
                        return "DropFiles";
                }
                return "Unknown";
            }
        };

        using ElementEvent = Delegate<UIEvent &>;
        class UILayout;
        class Widget;
        class UITheme;
        struct UIControlVisual;

        enum class EUIInvalidationReason : u32
        {
            kNone = 0u,
            kPaint = 1u << 0u,
            kLayout = 1u << 1u,
            kTransform = 1u << 2u,
            kHierarchy = 1u << 3u,
            kClip = 1u << 4u,
            kVisibility = 1u << 5u,
            kTextLayout = 1u << 6u
        };

        inline EUIInvalidationReason operator|(EUIInvalidationReason lhs, EUIInvalidationReason rhs)
        {
            return static_cast<EUIInvalidationReason>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
        }

        inline EUIInvalidationReason &operator|=(EUIInvalidationReason &lhs, EUIInvalidationReason rhs)
        {
            lhs = lhs | rhs;
            return lhs;
        }

        inline bool HasInvalidation(EUIInvalidationReason value, EUIInvalidationReason flag)
        {
            return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0u;
        }

        // ── Style 失效范围 ───────────────────────────────────────
        AENUM()
        enum class EStyleInvalidation : u8
        {
            kPaintOnly,       // 仅重绘（color, border 等）
            kLayoutAndPaint,  // 需重新布局 + 重绘（font_size, padding, min_size 等）
        };

        // ── Style 解析上下文 ─────────────────────────────────────
        struct UIStyleContext
        {
            const UITheme *_theme = nullptr;
            const UIElement *_parent = nullptr;
            Widget *_widget = nullptr;
            u64 _theme_revision = 0u;
        };

        // ── 元素交互状态位域（可组合）───────────────────────────
        AENUM()
        enum class EUIElementState : u32
        {
            kNone        = 0u,
            kHovered     = 1u << 0u,
            kPressed     = 1u << 1u,
            kFocused     = 1u << 2u,
            kEnabled     = 1u << 3u,
            kVisible     = 1u << 4u,
            kMouseEvents = 1u << 5u,
        };

        // ── 控件视觉状态（互斥）───────────────────────────────
        AENUM()
        enum class EUIVisualState
        {
            kNormal,
            kHovered,
            kPressed,
            kFocused,
            kDisabled
        };

        ACLASS()
        class AILU_API UIElement : public SerializeObject
        {
            GENERATED_BODY()
            DECLARE_DELEGATE(on_child_add, UIElement *);
            DECLARE_DELEGATE(on_child_remove, UIElement *);
            DECLARE_DELEGATE(on_focus_gained);
            DECLARE_DELEGATE(on_focus_lost);
            friend class UIManager;
        public:
            static bool IsPointInside(Vector2f point,Vector4f rect)
            {
                return point.x >= rect.x && point.x <= rect.x + rect.z &&
                       point.y >= rect.y && point.y <= rect.y + rect.w;
            }

            // ── 交互状态位域访问器（替代原 State 结构体）─────────────
            bool IsHovered() const;
            bool IsPressed() const;
            bool IsFocused() const;
            bool IsInteractiveEnabled() const;
            bool IsStateVisible() const;
            bool WantsMouseEvents() const;

            void SetHovered(bool v);
            void SetPressed(bool v);
            void SetInteractiveEnabled(bool v);
            void SetStateVisible(bool v);
            void SetWantsMouseEvents(bool v);
            void SetFocused(bool v);

            /// 从当前交互状态推导控件视觉状态（Disabled > Pressed > Hovered > Focused > Normal）
            EUIVisualState GetVisualState() const;

            // ── Style 解析（惰性触发，由 Measure / Render 入口驱动）────
            void EnsureStyleResolved();
            void InvalidateStyle(EStyleInvalidation invalidation = EStyleInvalidation::kLayoutAndPaint);

        protected:
            /// 子控件覆写：根据 Theme + Local Override 生成 resolved style
            virtual void ResolveStyle(const UIStyleContext &context) {}
            UIStyleContext BuildStyleContext() const;
            const UITheme *GetTheme() const;
            const UIControlVisual *GetCurrentVisual() const { return GetVisual(GetVisualState()); }
            virtual const UIControlVisual *GetVisual(EUIVisualState state) const { return nullptr; }

        private:
            bool _is_style_dirty = true;
            u64 _resolved_theme_revision = 0u;

        public:
            ~UIElement();
            UIElement();
            explicit UIElement(const String &name);
            void Serialize(FArchive &ar) override;
            void Deserialize(FArchive &ar) override;
            virtual void PostDeserialize() override;
            UIElement* AddChild(Ref<UIElement> child);
            template<typename T, typename... Args>
            T *AddChild(Args &&...args)
            {
                Ref<T> child = MakeRef<T>(std::forward<Args>(args)...);
                return dynamic_cast<T *>(AddChild(child));
            }
            template<typename T>
            T *As()
            {
                return dynamic_cast<T *>(this);
            }
            void RemoveChild(Ref<UIElement> child);
            void RemoveChild(UIElement* child);
            void ClearChildren();
            i32 IndexOf(UIElement *child);
            UIElement *ChildAt(u32 index);
            UIElement *operator[](u32 index) { return ChildAt(index); }
            void Render(UIRenderer &r);
            virtual void PreUpdate(f32 dt);
            virtual void Update(f32 dt);
            virtual void PostUpdate(f32 dt);
            virtual void OnEvent(UIEvent &e);
            /// <summary>
            /// 返回该ui元素及其子元素所需要的尺寸
            /// </summary>
            /// <returns>A Vector2f representing the desired width and height.</returns>
            virtual Vector2f MeasureDesiredSize();
            //容器类实际进行布局的地方
            virtual void MeasureAndArrange(f32 dt) {};
            void SetVisible(bool visible);
            [[nodiscard]] bool IsVisible() const;
            //分配局部空间
            void Arrange(f32 x, f32 y, f32 width, f32 height);
            void Arrange(Vector4f rect);
            /// <summary>
            /// 返回所分配的rect，基于屏幕空间,有旋转的话这个值就是无效的
            /// </summary>
            /// <returns>ltwh</returns>
            [[nodiscard]] Vector4f GetArrangeRect() const;
            /// <summary>
            /// 返回所分配内容区rect，基于屏幕空间,有旋转的话这个值就是无效的
            /// </summary>
            /// <returns>ltwh</returns>
            [[nodiscard]] Vector4f GetContentRect() const;
            void SetSlot(Ref<UISlot> slot);
            Ref<UISlot> &GetSlot()
            {
                EnsureSlotObject();
                return _slot_obj;
            }
            const Ref<UISlot> &GetSlot() const
            {
                EnsureSlotObject();
                return _slot_obj;
            }
            template<typename T>
            T &GetSlotAs()
            {
                T *slot = dynamic_cast<T *>(EnsureSlotObject().get());
                AL_ASSERT(slot != nullptr);
                return *slot;
            }
            void SetDepth(f32 depth);
            ElementEvent::EventView OnMouseEnter();
            ElementEvent::EventView OnMouseExit();
            ElementEvent::EventView OnMouseDown();
            ElementEvent::EventView OnMouseUp();
            ElementEvent::EventView OnMouseDoubleClick();
            ElementEvent::EventView OnMouseClick();
            ElementEvent::EventView OnMouseMove();
            ElementEvent::EventView OnKeyDown();
            ElementEvent::EventView OnKeyUp();
            ElementEvent::EventView OnMouseScroll();
            ElementEvent::EventView OnFileDrop();
            //pos
            bool IsPointInside(Vector2f  mouse_pos) const;
            Vector2f GetWorldPosition() const;
            virtual UIElement* HitTest(Vector2f pos);
            //层级管理
            auto begin() { return _children.begin(); }
            auto end() { return _children.end(); }
            [[nodiscard]] u16 GetHierarchyDepth() const { return _hierarchy_depth; }
            [[nodiscard]] UIElement* GetParent() const { return _parent; }
            void SetParent(UIElement *parent) { _parent = parent; };
            [[nodiscard]] const Vector<Ref<UIElement>> &GetChildren() const { return _children; }
            [[nodiscard]] f32 GetDepth() const { return _depth; }
            //焦点管理
            void RequestFocus();// 主动请求焦点
            //默认向上传递dirty,true则只标记子元素
            void InvalidatePaint();
            void InvalidateLayout(bool propagate_down = false);
            void InvalidateTransform();
            void InvalidateHierarchy();
            void ClearPaintDirtyRecursive();
            void ClearDebugPaintDirtyRecursive();
            void SnapshotPaintDirtyToDebugRecursive();
            bool IsPaintDirty() const { return _paint_dirty; }
            bool IsDebugPaintDirty() const { return _debug_paint_dirty; }
            bool IsLayoutDirty() const { return _is_layout_dirty; }
            bool IsTransformDirty() const { return _is_transf_dirty; }
            EUIInvalidationReason GetInvalidationReasons() const { return _dirty_reasons; }
            EUIInvalidationReason GetDebugInvalidationReasons() const { return _debug_dirty_reasons; }
            void SetOwningWidgetRecursive(Widget *widget);
            Widget *GetOwningWidget() const { return _owning_widget; }
            //内边距
            Padding &SlotPadding() { return _padding; }
            Padding SlotPadding() const { return _padding; }
            //变换
            void Translate(f32 x, f32 y)
            {
                Translate({x, y});
            }
            void Translate(Vector2f pos)
            {
                if (NearbyEqual(pos, _transition))
                    return;
                _transition = pos;
                InvalidateTransform();
            }
            //angle
            void Rotate(f32 degree)
            {
                if (NearbyEqual(degree, _rotation))
                    return;
                _rotation = degree;
                InvalidateTransform();
            }
            void Scale(f32 sx, f32 sy)
            {
                Scale({sx, sy});
            }
            void Scale(Vector2f s)
            {
                if (NearbyEqual(s, _scale))
                    return;
                _scale = {s.x, s.y};
                InvalidateTransform();
            }
            Math::Transform2D GetTransform() const { return _transform; }
            void SetDropHandler(DropHandler handler) { _drop_handler = std::move(handler); }
            DropHandler *GetDropHandler() { return _drop_handler ? &_drop_handler.value() : nullptr; }
            void AddPropertyObserver(PropertyObserverHandle&& handle)
            {
                _property_observers.emplace_back(std::move(handle));
            }
        public:
            APROPERTY()
            EVisibility _visibility = EVisibility::kVisible;
            u32 _state_flags = (u32)EUIElementState::kEnabled | (u32)EUIElementState::kVisible | (u32)EUIElementState::kMouseEvents;
        private:
            void SetFocusedInternal(bool v);// 仅 UIManager 使用
            void ApplyTransform();
            Ref<UISlot> &EnsureSlotObject() const;
            virtual Ref<UISlot> CreateSlotForChild();
            virtual bool UsesVerticalChildLayout() const { return false; }
        protected:
            void OnPropertyChanged(const PropertyInfo& prop) override;
            virtual void RenderImpl(UIRenderer &r) {};
            /// <summary>
            /// Calculates the world transformation matrix for the object.
            /// </summary>
            /// <param name="is_exclude_self_offset">是否包含content_rect.xy到变换中，对于当前元素不写入，后续使用ui render接口时直接传入content_rect，避免偏移叠加，但是上层的需要</param>
            /// <returns>The computed world transformation matrix as a Matrix4x4f object.</returns>
            Matrix4x4f CalculateWorldMatrix(bool is_exclude_self_offset = true) const;
            virtual void PostArrange() {};
        protected:
            APROPERTY()
            mutable Ref<UISlot> _slot_obj;
            APROPERTY()
            Padding _padding;      // ltrb，元素内边距
            Vector4f _desired_rect;//元素所需要的rect，一般而言就是slot.pos/size
            Vector4f _content_rect;//内容区域，_desired_rect受padding影响的结果,局部空间
            Vector4f _arrange_rect;//元素最终被布局分配到的rect,局部空间
            Vector4f _abs_rect;    //元素最终被布局分配到的rect,相对于窗口，有旋转的话这个值就是无效的
            UIElement* _parent = nullptr;
            Widget *_owning_widget = nullptr;
            Vector<Ref<UIElement>> _children;
            u16 _hierarchy_depth = 0u;
            bool _is_visible;
            f32 _depth;
            HashMap<UIEvent::EType, ElementEvent> _eventmap;
            APROPERTY()
            Vector2f _transition = Vector2f::kZero;
            APROPERTY()
            f32 _rotation = 0.f;// in degree
            APROPERTY()
            Vector2f _scale = Vector2f::kOne;
            Math::Transform2D _transform;
            Matrix4x4f _matrix,_inv_matrix;
            /*
            1. 父布局变化，子节点向下传递
            2. 子节点属性变化，向上标记
            3.paint_dirty之后用来标记缓存顶点数据
            */
            bool _is_layout_dirty = true, _paint_dirty = true, _is_transf_dirty = true;
            EUIInvalidationReason _dirty_reasons = EUIInvalidationReason::kPaint;
            bool _debug_paint_dirty = false;
            EUIInvalidationReason _debug_dirty_reasons = EUIInvalidationReason::kNone;
            std::optional<DropHandler> _drop_handler;
            Vector<PropertyObserverHandle> _property_observers;
        };
        /// <summary>
        /// 除了AnchorLayout之外，所有元素都使用左上角作为锚点
        /// </summary>
        class AILU_API Panel : public UIElement
        {
        public:
            explicit Panel(UILayout *layout) : _layout(layout) {};
            UILayout *_layout = nullptr;
        };
    }// namespace UI
}// namespace Ailu

#endif//AILU_UIELEMENT_H
