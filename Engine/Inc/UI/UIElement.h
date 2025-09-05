//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIELEMENT_H
#define AILU_UIELEMENT_H

#include "Framework/Math/Transform2D.h"
#include "Framework/Events/Event.h"
#include "Objects/Serialize.h"
#include <Render/RendererAPI.h>
#include "Framework/Common/Allocator.hpp"
#include "generated/UIElement.gen.h"

namespace Ailu
{
    namespace UI
    {
        AENUM()
        enum class EAlignment
        {
            kLeft,
            kCenter,
            kRight,
            kTop,
            kBottom,
            kFill
        };

        AENUM()
        enum class ESlotType
        {
            kCanvas,
            kVerticalBox,
            kHorizontalBox
        };
        //用于vertical/horizontal box布局
        AENUM()
        enum class ESizePolicy
        {
            kFixed,//使用slot.size即使不是canvas
            kFill, // 填充剩余空间,如果有多个fill则平分剩余空间 
            kAuto  // 根据内容自适应大小
        };

        ASTRUCT()
        struct Padding
        {
            GENERATED_BODY()
            f32 _l = 0.f;
            f32 _t = 0.f;
            f32 _r = 0.f;
            f32 _b = 0.f;
            Padding() = default;
            Padding(f32 l, f32 t, f32 r, f32 b) : _l(l), _t(t), _r(r), _b(b) {};
            Padding(f32 v) : Padding(v, v, v, v) {};
            Padding(Vector4f v) : Padding(v.x, v.y, v.z, v.w) {};
            void Serialize(FArchive &ar)
            {
                Vector4f v{_l, _t, _r, _b};
                SerializePrimitive<Vector4f>(&v, ar);
            };
            void Deserialize(FArchive &ar)
            {
                Vector4f v;
                DeserializePrimitive<Vector4f>(&v, ar);
                memcpy(this, &v, sizeof(Padding));
            };
        };

        ASTRUCT()
        struct Slot
        {
            GENERATED_BODY()
            Slot(Vector2f size) : _anchor(), _position(), _size(size) {};
            Slot() : Slot({100.f, 100.f}) {};
            APROPERTY()
            Vector2f _size;
            APROPERTY()
            Vector2f _anchor = Vector2f::kZero;
            APROPERTY()
            Vector2f _position = Vector2f::kZero;
            APROPERTY()
            EAlignment _alignment_h = EAlignment::kLeft;
            APROPERTY()
            EAlignment _alignment_v = EAlignment::kCenter;
            APROPERTY()
            Padding _margin;// ltrb，元素外边距
            APROPERTY()
            ESlotType _type = ESlotType::kCanvas;
            APROPERTY()
            ESizePolicy _size_policy = ESizePolicy::kAuto;//用于linear box slot
            APROPERTY()
            bool _is_size_to_content = false;//用于canvas slot
            APROPERTY()
            f32 _fill_rate = 1.0f;//linear box slot,所有fill的fill_rate之和为总权重
        }; 

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
                kKeyUp
            };

            EType _type;
            Vector2f _mouse_position;
            Vector2f _mouse_delta;
            UIElement *_target = nullptr;
            UIElement *_current_target = nullptr;// 用于冒泡阶段
            u32 _key_code = 0u;                  // 按键事件使用
            bool _is_handled = false;
            f32 _scroll_delta = 0.0f;// 滚轮滚动增量

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
                }
                return "Unknown";
            }
        };

        using ElementEvent = Delegate<UIEvent &>;
        class UILayout;

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
            struct State
            {
                bool _is_hovered = false;       // 鼠标在控件内部
                bool _is_pressed = false;       // 鼠标按下状态
                bool _is_focused = false;       // 当前控件获取焦点
                bool _is_enabled = true;        // 控件是否可交互
                bool _is_visible = true;        // 控件是否可见
                bool _wants_mouse_events = true; // 是否接受鼠标事件
            };
        public:
            static bool IsPointInside(Vector2f point,Vector4f rect)
            {
                return point.x >= rect.x && point.x <= rect.x + rect.z &&
                       point.y >= rect.y && point.y <= rect.y + rect.w;
            }
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
            bool IsFocused() const { return _state._is_focused; }
            void InvalidateLayout();
            void InvalidateTransform();
            //插槽属性
            FORCEINLINE UIElement& SlotPosition(Vector2f pos)
            {
                _slot._position = pos;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotSize(f32 w, f32 h)
            {
                SlotSize({w, h});
                return *this;
            }
            FORCEINLINE UIElement& SlotSize(Vector2f size)
            {
                _slot._size = size;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotAnchor(Vector2f anchor)
            {
                _slot._anchor = anchor;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotAlignmentH(EAlignment h)
            {
                _slot._alignment_h = h;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotAlignmentV(EAlignment v)
            {
                _slot._alignment_v = v;
                InvalidateLayout();
                return *this;
            }
            //外边距
            FORCEINLINE UIElement& SlotMargin(Padding margin)
            {
                _slot._margin = margin;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotSizePolicy(ESizePolicy policy)
            {
                _slot._size_policy = policy;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotSizeToContent(bool v)
            {
                _slot._is_size_to_content = v;
                InvalidateLayout();
                return *this;
            }
            FORCEINLINE UIElement& SlotFillRate(f32 rate)
            {
                _slot._fill_rate = rate;
                InvalidateLayout();
                return *this;
            }
            //内边距
            FORCEINLINE UIElement& SlotPadding(Padding padding)
            {
                _padding = padding;
                InvalidateLayout();
                return *this;
            }
            Vector2f SlotPosition() const { return _slot._position; }
            Vector2f SlotSize() const { return _slot._size; }
            Vector2f SlotAnchor() const { return _slot._anchor; }
            EAlignment SlotAlignmentH() const { return _slot._alignment_h; }
            EAlignment SlotAlignmentV() const { return _slot._alignment_v; }
            Padding SlotMargin() const { return _slot._margin; }
            ESizePolicy SlotSizePolicy() const { return _slot._size_policy; }
            bool SlotSizeToContent() const { return _slot._is_size_to_content; }
            f32 SlotFillRate() const { return _slot._fill_rate; }
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
        public:
            APROPERTY()
            Padding _padding;// ltrb，元素内边距
            APROPERTY()
            EVisibility _visibility = EVisibility::kVisible;
            State _state;
        private:
            void SetFocusedInternal(bool v);// 仅 UIManager 使用
            void ApplyTransform();
        protected:
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
            Slot _slot;
            Vector4f _desired_rect;//元素所需要的rect，一般而言就是slot.pos/size
            Vector4f _content_rect;//内容区域，_desired_rect受padding影响的结果,局部空间
            Vector4f _arrange_rect;//元素最终被布局分配到的rect,局部空间
            Vector4f _abs_rect;    //元素最终被布局分配到的rect,相对于窗口，有旋转的话这个值就是无效的
            UIElement* _parent = nullptr;
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
            bool _is_layout_dirty = true, _paint_dirty = true,_is_transf_dirty;
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
