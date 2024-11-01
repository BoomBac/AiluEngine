//
// Created by 22292 on 2024/10/29.
//

#ifndef AILU_CANVAS_H
#define AILU_CANVAS_H
#include "UIElement.h"
#include "Render/Texture.h"
namespace Ailu
{
    namespace UI
    {
        class AILU_API Canvas : public UIElement
        {
        public:
            struct Slot
            {
                Vector2f _size;
                Vector2f _anchor;
                Vector2f _position;
                Vector2f _alignment;
                explicit Slot(Vector2f size) : _anchor(), _position(), _alignment(), _size(size) {};
                Slot(): Slot({100.f,100.f}){};
            };
        public:
            Canvas();
            void AddChild(UIElement* child, Slot slot);
            void RemoveChild(UIElement* child) final;
            Slot& GetSlot(UIElement* child);
            void Update(f32 dt) final;
            void Render() final;
            void OnEvent(Ailu::Event& event);
            RenderTexture* GetRenderTexture() const;
            Vector2f GetSize() const;
            void SetPosition(Vector2f position);
        private:
            Map<u32,Slot> _slots;
            Vector2f _size;
            Vector2f _position;
            f32 _scale;
            Scope<RenderTexture> _color, _depth;
        };

    }// namespace UI
}// namespace Ailu

#endif//AILU_CANVAS_H
