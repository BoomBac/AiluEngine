#include "UI/Container.h"
#include "UI/UIRenderer.h"
#include "pch.h"

namespace Ailu
{
    namespace UI
    {
#undef max
#pragma region Canvas
        Canvas::Canvas() : UIElement("Canvas")
        {
            _desired_rect = Vector4f(0.0f, 0.0f, 100.0f, 100.0f);
        }
        Canvas::~Canvas()
        {
        }

        void Canvas::Update(f32 dt)
        {
            for (auto &c: _children)
            {
                c->Update(dt);
                const auto &slot = c->_slot;
                c->SetDesiredRect(slot._position.x + _desired_rect.x, slot._position.y + _desired_rect.y, slot._size.x, slot._size.y);
            }
        }

        void Canvas::Render(UIRenderer &r)
        {
            for (auto &c: _children)
            {
                const auto &slot = c->_slot;
                c->Render(r);
            }
        }
#pragma endregion

#pragma region LinearBox
        void LinearBox::Render(UIRenderer &r)
        {
            for (auto &c: _children)
            {
                const auto &slot = c->_slot;
                c->Render(r);
            }
        }

        void LinearBox::UpdateLayout(f32 dt)
        {
            const f32 element_count = static_cast<f32>(_children.size());
            if (element_count <= 0.0f)
                return;

            // ---------- SizeToContent 模式 ----------
            if (_is_size_to_content)
            {
                f32 total_len = 0.0f;
                f32 max_cross = 0.0f;

                for (auto &c: _children)
                {
                    const auto &slot = c->_slot;
                    const auto &margin = slot._margin;
                    Vector2f child_size = slot._size;

                    if (_orientation == EOrientation::kVertical)
                    {
                        total_len += margin._t + child_size.y + margin._b;
                        max_cross = std::max(max_cross, child_size.x + margin._l + margin._r);
                    }
                    else
                    {
                        total_len += margin._l + child_size.x + margin._r;
                        max_cross = std::max(max_cross, child_size.y + margin._t + margin._b);
                    }
                }

                if (_orientation == EOrientation::kVertical)
                {
                    _slot._size.x = max_cross + _padding._l + _padding._r;
                    _slot._size.y = total_len + _padding._t + _padding._b;
                }
                else
                {
                    _slot._size.x = total_len + _padding._l + _padding._r;
                    _slot._size.y = max_cross + _padding._t + _padding._b;
                }
            }

            // ---------- 可用区域 ----------
            f32 available_w = _content_rect.z;
            f32 available_h = _content_rect.w;

            // 统计 fill 元素
            u32 fill_count = 0;
            f32 fixed_total = 0.0f;

            for (auto &c: _children)
            {
                const auto &slot = c->_slot;
                const auto &margin = slot._margin;

                if (slot._is_fill_size)
                {
                    fill_count++;
                }
                else
                {
                    if (_orientation == EOrientation::kVertical)
                        fixed_total += margin._t + slot._size.y + margin._b;
                    else
                        fixed_total += margin._l + slot._size.x + margin._r;
                }
            }

            // 平均分配 fill
            f32 fill_len = 0.0f;
            if (_orientation == EOrientation::kVertical)
                fill_len = (fill_count > 0) ? std::max(0.0f, (available_h - fixed_total) / fill_count) : 0.0f;
            else
                fill_len = (fill_count > 0) ? std::max(0.0f, (available_w - fixed_total) / fill_count) : 0.0f;

            // ---------- 子元素布局 ----------
            f32 offset = 0.0f;

            for (auto &c: _children)
            {
                auto &slot = c->_slot;
                auto &margin = slot._margin;

                f32 child_w = 0.0f;
                f32 child_h = 0.0f;
                f32 x = _content_rect.x;
                f32 y = _content_rect.y;

                if (_orientation == EOrientation::kVertical)
                {
                    child_h = slot._is_fill_size ? fill_len : slot._size.y;
                    child_w = available_w - margin._l - margin._r;
                    y += offset + margin._t;
                    x += margin._l;

                    // 横向对齐
                    switch (slot._alignment_h)
                    {
                        case EAlignment::kFill:
                            break;
                        case EAlignment::kLeft:
                            child_w = slot._size.x;
                            break;
                        case EAlignment::kCenter:
                            child_w = slot._size.x;
                            x = _content_rect.x + margin._l + (available_w - child_w - margin._l - margin._r) * 0.5f;
                            break;
                        case EAlignment::kRight:
                            child_w = slot._size.x;
                            x = _content_rect.x + available_w - child_w - margin._r;
                            break;
                    }

                    c->SetDesiredRect(x, y, child_w, child_h);
                    offset += margin._t + child_h + margin._b;
                }
                else// Horizontal
                {
                    child_w = slot._is_fill_size ? fill_len : slot._size.x;
                    child_h = available_h - margin._t - margin._b;
                    x += offset + margin._l;
                    y += margin._t;

                    // 纵向对齐
                    switch (slot._alignment_v)
                    {
                        case EAlignment::kFill:
                            break;
                        case EAlignment::kTop:
                            child_h = slot._size.y;
                            break;
                        case EAlignment::kCenter:
                            child_h = slot._size.y;
                            y = _content_rect.y + margin._t + (available_h - child_h - margin._t - margin._b) * 0.5f;
                            break;
                        case EAlignment::kBottom:
                            child_h = slot._size.y;
                            y = _content_rect.y + available_h - child_h - margin._b;
                            break;
                    }

                    c->SetDesiredRect(x, y, child_w, child_h);
                    offset += margin._l + child_w + margin._r;
                }

                c->Update(dt);
            }

#pragma endregion
        }// namespace UI
    }// namespace UI
}// namespace Ailu
