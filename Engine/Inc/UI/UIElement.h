//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIELEMENT_H
#define AILU_UIELEMENT_H

#include "Framework/Math/ALMath.hpp"
#include "Objects/Object.h"
#include <Render/RendererAPI.h>
#include "Framework/Common/Allocator.hpp"

namespace Ailu
{
    namespace UI
    {
        struct RectTransform
        {
            Vector2f _translation = Vector2f::kZero;
            Vector2f _scale = Vector2f::kOne;
            Vector2f _anchor = Vector2f::kZero;
            Vector2f _shear = Vector2f::kZero;
            f32 _rotation = 0.f;
            f32 _padding = 0.f;
        };
        class UIElement;
        class UIRenderer;
        struct UIEvent
        {
            enum class EType
            {
                kMouseEnter,
                kMouseExit,
                kMouseDown,
                kMouseUp,
                kMouseDoubleClick,
                kMouseClick,
                kMouseMove
            };
            EType _type;
            Vector2f _mouse_position;
            UIElement* _target;
            UIElement* _currentTarget;// 用于冒泡阶段
            bool _is_handled = false;
            bool operator==(const UIEvent &other) const
            {
                return _type == other._type && _mouse_position == other._mouse_position &&
                       _target == other._target && _currentTarget == other._currentTarget;
            }
        };

        class Event
        {
            friend class UIElement;
            friend class Canvas;
        public :
            using Callback = std::function<void(UIEvent&)>;
            void AddListener(const Callback &callback);
            void RemoveListener(const Callback &callback);
            void operator+=(const Callback &callback);
            void operator-=(const Callback &callback);
        private:
            void Invoke(UIEvent &e);
            void Clear();
        private:
            Vector<Callback> _callbacks;
        };
        class UILayout;

        DECLARE_ENUM(EElementState,kNormal,kHover,kPressed,kDisabled)
        class AILU_API UIElement : public Object
        {
        public:
            template<class ElementType, typename... Args>
            static ElementType *Create(Args &&...args)
            {
                return AL_NEW(ElementType, std::forward<Args>(args)...);
            }
            static void Destroy(UIElement *element)
            {
                AL_DELETE(element);
            }
            UIElement();
            ~UIElement();
            explicit UIElement(const String &name);
            virtual void AddChild(UIElement *child);
            virtual void RemoveChild(UIElement *child);
            RectTransform& GetRect();
            virtual void Update(f32 dt);
            virtual void Render(UIRenderer &r);
            virtual void OnEvent(UIEvent &e);
            void SetVisible(bool visible);
            [[nodiscard]] bool IsVisible() const;
            void SetDesiredRect(f32 x, f32 y, f32 width, f32 height);
            /// <summary>
            /// ltwh
            /// </summary>
            /// <returns></returns>
            [[nodiscard]] Vector4f GetDesiredRect() const;
            void SetDepth(f32 depth);
            Event& OnMouseEnter();
            Event& OnMouseExit();
            Event& OnMouseDown();
            Event& OnMouseUp();
            Event& OnMouseDoubleClick();
            Event& OnMouseClick();
            Event& OnMouseMove();
            void SetElementState(EElementState::EElementState state);
            [[nodiscard]] EElementState::EElementState GetState() const;
            //pos
            bool IsPointInside(Vector2f  mouse_pos) const;
            Vector2f GetWorldPosition() const;
            RectTransform GetWorldRect() const;
            //层级管理
            auto begin() { return _children.begin(); }
            auto end() { return _children.end(); }
            [[nodiscard]] u16 GetHierarchyDepth() const { return _hierarchy_depth; }
            [[nodiscard]] UIElement* GetParent() const { return _parent; }
            void SetParent(UIElement *parent) { _parent = parent; };
            [[nodiscard]] const Vector<UIElement*>& GetChildren() const { return _children; }
            [[nodiscard]] f32 GetDepth() const { return _depth; }
        private:
        protected:
            RectTransform _rect;
            Vector4f _desired_rect;
            Vector<UIElement *> _children;
            u16 _hierarchy_depth = 0u;
            UIElement* _parent = nullptr;
            EElementState::EElementState _state = EElementState::kNormal;
            bool _is_visible;
            f32 _depth;
            HashMap<UIEvent::EType, Event> _eventmap;
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

        class AILU_API Button : public UIElement
        {
        public:
            Button();
            explicit Button(const String &name);
            void Update(f32 dt) override;
            void Render(UIRenderer &r) override;
        };

    }// namespace UI
}// namespace Ailu

#endif//AILU_UIELEMENT_H
