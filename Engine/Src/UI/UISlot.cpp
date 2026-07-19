#include "UI/UISlot.h"
#include "UI/UIElement.h"
#include "pch.h"

namespace Ailu
{
    namespace UI
    {
        UISlot &UISlot::Margin(const Padding &margin)
        {
            if (_margin._l == margin._l && _margin._t == margin._t && _margin._r == margin._r && _margin._b == margin._b)
                return *this;
            _margin = margin;
            InvalidateLayout();
            return *this;
        }

        UISlot &UISlot::Size(const Vector2f &size)
        {
            if (NearbyEqual(_size, size))
                return *this;
            _size = size;
            InvalidateLayout();
            return *this;
        }

        void UISlot::SetOwner(UIElement *owner)
        {
            _owner = owner;
        }

        void UISlot::InvalidateLayout()
        {
            if (_owner != nullptr)
                _owner->InvalidateLayout();
        }

        void UISlot::PostPropertyChanged()
        {
            InvalidateLayout();
        }

        void UISlot::OnPropertyChanged(const PropertyInfo &prop)
        {
            Object::OnPropertyChanged(prop);
            PostPropertyChanged();
        }

        CanvasSlot &CanvasSlot::Margin(const Padding &margin)
        {
            UISlot::Margin(margin);
            return *this;
        }

        CanvasSlot &CanvasSlot::Size(const Vector2f &size)
        {
            UISlot::Size(size);
            return *this;
        }

        CanvasSlot &CanvasSlot::Position(const Vector2f &position)
        {
            if (NearbyEqual(_position, position))
                return *this;
            _position = position;
            InvalidateLayout();
            return *this;
        }

        CanvasSlot &CanvasSlot::Anchor(const Vector2f &anchor)
        {
            if (NearbyEqual(_anchor, anchor))
                return *this;
            _anchor = anchor;
            InvalidateLayout();
            return *this;
        }

        CanvasSlot &CanvasSlot::SizeToContent(bool value)
        {
            if (_size_to_content == value)
                return *this;
            _size_to_content = value;
            InvalidateLayout();
            return *this;
        }

        CanvasSlot &CanvasSlot::Alignment(EAlignment horizontal, EAlignment vertical)
        {
            if (_alignment_h == horizontal && _alignment_v == vertical)
                return *this;
            _alignment_h = horizontal;
            _alignment_v = vertical;
            InvalidateLayout();
            return *this;
        }

        LinearSlot &LinearSlot::Margin(const Padding &margin)
        {
            UISlot::Margin(margin);
            return *this;
        }

        LinearSlot &LinearSlot::Size(const Vector2f &size)
        {
            UISlot::Size(size);
            return *this;
        }

        LinearSlot &LinearSlot::SizePolicy(ESizePolicy horizontal, ESizePolicy vertical)
        {
            if (_size_policy_h == horizontal && _size_policy_v == vertical)
                return *this;
            _size_policy_h = horizontal;
            _size_policy_v = vertical;
            InvalidateLayout();
            return *this;
        }

        LinearSlot &LinearSlot::FillRate(f32 fill_rate)
        {
            fill_rate = std::max(0.0f, fill_rate);
            if (NearbyEqual(_fill_rate, fill_rate))
                return *this;
            _fill_rate = fill_rate;
            InvalidateLayout();
            return *this;
        }

        LinearSlot &LinearSlot::CrossAlignment(EAlignment alignment)
        {
            if (_cross_align == alignment)
                return *this;
            _cross_align = alignment;
            InvalidateLayout();
            return *this;
        }
    }// namespace UI
}// namespace Ailu
