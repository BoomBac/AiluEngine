//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIELEMENT_H
#define AILU_UIELEMENT_H

#include "Framework/Math/ALMath.hpp"
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

        ASTRUCT()
        struct Padding
        {
            GENERATED_BODY()
            f32 _l = 0.f;
            f32 _t = 0.f;
            f32 _r = 0.f;
            f32 _b = 0.f;
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
            bool _is_fill_size = false;// 是否填充父容器在主轴上的剩余空间
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
                kMouseExitWindow
            };
            EType _type;
            Vector2f _mouse_position;
            Vector2f _mouse_delta;
            UIElement* _target = nullptr;
            UIElement* _current_target= nullptr;// 用于冒泡阶段
            bool _is_handled = false;
            bool operator==(const UIEvent &other) const
            {
                return _type == other._type && _mouse_position == other._mouse_position &&
                       _target == other._target && _current_target == other._current_target;
            }
        };
        using ElementEvent = Delegate<UIEvent &>;
        class UILayout;

        ACLASS()
        class AILU_API UIElement : public SerializeObject
        {
            GENERATED_BODY()
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
            UIElement* AddChild(Ref<UIElement> child);
            template<typename T, typename... Args>
            T *AddChild(Args &&...args)
            {
                Ref<T> child = MakeRef<T>(std::forward<Args>(args)...);
                return dynamic_cast<T *>(AddChild(child));
            }

            void RemoveChild(Ref<UIElement> child);
            virtual void Update(f32 dt);
            virtual void Render(UIRenderer &r);
            virtual void OnEvent(UIEvent &e);
            void SetVisible(bool visible);
            [[nodiscard]] bool IsVisible() const;
            void SetDesiredRect(f32 x, f32 y, f32 width, f32 height);
            void SetDesiredRect(Vector4f rect);
            /// <summary>
            /// ltwh
            /// </summary>
            /// <returns></returns>
            [[nodiscard]] Vector4f GetDesiredRect() const;
            void SetDepth(f32 depth);
            ElementEvent& OnMouseEnter();
            ElementEvent& OnMouseExit();
            ElementEvent& OnMouseDown();
            ElementEvent& OnMouseUp();
            ElementEvent& OnMouseDoubleClick();
            ElementEvent& OnMouseClick();
            ElementEvent& OnMouseMove();
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
            
        public:
            APROPERTY()
            Slot _slot;
            APROPERTY()
            Padding _padding;// ltrb，元素内边距
            State _state;
        private:
        protected:
            Delegate<UIElement *> _on_child_add;
            Delegate<UIElement *> _on_child_remove;
            Vector4f _desired_rect,_content_rect;
            UIElement* _parent = nullptr;
            Vector<Ref<UIElement>> _children;
            u16 _hierarchy_depth = 0u;
            bool _is_visible;
            f32 _depth;
            HashMap<UIEvent::EType, ElementEvent> _eventmap;
            
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
