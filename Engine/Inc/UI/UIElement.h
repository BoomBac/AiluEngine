//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UIELEMENT_H
#define AILU_UIELEMENT_H

#include "Framework/Math/ALMath.hpp"
#include "Objects/Object.h"
#include "Framework/Events/Event.h"
#include <Render/RendererAPI.h>

namespace Ailu
{
    namespace UI
    {
        struct RectTransform
        {
            Vector2f _translation = Vector2f::kZero;
            Vector2f _scale = Vector2f::kOne;
            Vector2f _pivot = Vector2f::kZero;
            Vector2f _shear = Vector2f::kZero;
            f32 _rotation = 0.f;
            f32 _padding = 0.f;
        };
        class Event
        {
            friend class UIElement;
            friend class Canvas;
        public :
            using Callback = std::function<void(const Ailu::Event&)>;
            void AddListener(const Callback &callback);
            void RemoveListener(const Callback &callback);
            void operator+=(const Callback &callback);
            void operator-=(const Callback &callback);
        private:
            void Invoke(const Ailu::Event& e);
            void Clear();
        private:
            Vector<Callback> _callbacks;
        };
//        template<class T>
//        class Property
//        {
//        public:
//            void Set(){
//                onValueChange.Invoke(_value);
//            };
//            void Get();
//        private:
//            T _value;
//        };
        DECLARE_ENUM(EElementState,kNormal,kHover,kPressed,kDisabled)
        class AILU_API UIElement : public Object
        {
        public:
            UIElement();
            ~UIElement();
            explicit UIElement(const String &name);
            virtual void AddChild(UIElement *child);
            virtual void RemoveChild(UIElement *child);
            RectTransform& GetRect();
            virtual void Update(f32 dt);
            virtual void Render();
            void SetVisible(bool visible);
            [[nodiscard]] bool IsVisible() const;
            void SetDeservedRect(f32 x, f32 y, f32 width, f32 height);
            [[nodiscard]] Vector4f GetDeservedRect() const;
            bool IsMouseOver(Vector2f  mouse_pos) const;
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
        private:
        protected:
            RectTransform _rect;
            Vector4f _deserved_rect;
            Vector<UIElement *> _children;
            u16 _hierarchy_depth = 0u;
            UIElement* _parent = nullptr;
            EElementState::EElementState _state = EElementState::kNormal;
            bool _is_visible;
            f32 _depth;
            Event _on_mouse_enter;
            Event _on_mouse_exit;
            Event _on_mouse_down;
            Event _on_mouse_up;
            Event _on_mouse_click;
            Event _on_mouse_double_click;
            Event _on_mouse_move;
        };

        class AILU_API Button : public UIElement
        {
        public:
            Button();
            explicit Button(const String &name);
            void Update(f32 dt) override;
            void Render() override;
        };

    }// namespace UI
}// namespace Ailu

#endif//AILU_UIELEMENT_H
