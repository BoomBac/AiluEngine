//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_CANVAS_H
#define AILU_CANVAS_H

#include "UILayout.h"
#include "UIElement.h"
#include "Render/Texture.h"
#include "generated/Canvas.gen.h"

namespace Ailu
{
    namespace UI
    {
        ASTRUCT()
        struct Slot
        {
            GENERATED_BODY()
            Slot(Vector2f size) : _anchor(), _position(), _size(size), _alignment(EAlignment::kLeft) {};
            Slot() : Slot({100.f, 100.f}) {};
            APROPERTY()
            Vector2f _size;
            APROPERTY()
            Vector2f _anchor;
            APROPERTY()
            Vector2f _position;
            APROPERTY()
            EAlignment _alignment;
        };
        ASTRUCT()
        struct SlotItem
        {
            GENERATED_BODY()
            APROPERTY()
            Slot _slot;
            APROPERTY()
            UIElement* _element;
        };
        /// <summary>
        /// 画布管理其所有ui元素的布局和渲染以及生命周期
        /// </summary>
        ACLASS()
        class AILU_API Canvas : public Object
        {
            GENERATED_BODY()
        public:
            Canvas();
            void BindOutput(Render::RenderTexture *color, Render::RenderTexture *depth = nullptr);
            void AddChild(UIElement* child, Slot slot);
            void RemoveChild(UIElement* child);
            Slot& GetSlot(UIElement* child);
            void Update();
            void Render(UIRenderer &r);
            void OnEvent(UIEvent& event);
            std::tuple<Render::RenderTexture *, Render::RenderTexture *> GetOutput() 
            {
                return _is_external_output ? std::make_tuple(_external_color, _external_depth) : std::make_tuple(_color.get(), _depth.get());
            };
            Vector2f GetSize() const;
            void SetPosition(Vector2f position);
            auto begin() { return _children.begin(); }
            auto end() { return _children.end(); }
        public:
            bool _is_external_output = false;
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
            HashMap<u32, SlotItem> _slots;
            Vector<UIElement *> _children;
        };

    }// namespace UI
}// namespace Ailu

#endif//AILU_CANVAS_H
