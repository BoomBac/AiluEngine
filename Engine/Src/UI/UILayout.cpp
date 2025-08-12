#include "UI/UILayout.h"
#include "UI/UIElement.h"

namespace Ailu
{
    namespace UI
    {
        void VerticalLayout::UpdateLayout(Panel *panel)
        {
            if (!panel)
                return;

            auto ltwh = panel->GetDesiredRect();// x, y, width, height
            f32 y_cursor = ltwh.y + _padding.y;

            for (auto *child: *panel)
            {
                if (!child || !child->IsVisible())
                    continue;

                auto child_rect = child->GetDesiredRect();
                f32 child_width = child_rect.z;
                f32 child_height = child_rect.w;

                Vector4f new_rect;

                // ---- Fill Mode 控制宽度 ----
                switch (_fill_mode)
                {
                    case ELayoutFillMode::kKeepSelf:
                        child_width = child_rect.z;
                        break;

                    case ELayoutFillMode::kFitParent:
                        child_width = ltwh.z - _padding.x - _padding.z;
                        break;

                    case ELayoutFillMode::kPercentParent:
                        child_width = (ltwh.z - _padding.x - _padding.z) * _percent;
                        break;

                    default:
                        AL_ASSERT(false);
                        break;
                }

                // ---- 对齐控制 ----
                if (_align == EAlignment::kLeft)
                {
                    new_rect.x = ltwh.x + _padding.x;
                }
                else if (_align == EAlignment::kCenter)
                {
                    f32 content_width = ltwh.z - _padding.x - _padding.z;
                    new_rect.x = ltwh.x + _padding.x + (content_width - child_width) * 0.5f;
                }
                else if (_align == EAlignment::kRight)
                {
                    new_rect.x = ltwh.x + ltwh.z - _padding.z - child_width;
                }

                new_rect.y = y_cursor;
                new_rect.z = child_width;
                new_rect.w = child_height;

                child->SetDesiredRect(new_rect.x, new_rect.y, new_rect.z, new_rect.w);

                y_cursor += child_height + _spacing;
            }
        }

        void HorizontalLayout::UpdateLayout(Panel *panel)
        {
            if (!panel)
                return;

            Vector4f ltwh = panel->GetDesiredRect();// x, y, width, height
            f32 x_cursor = ltwh.x + _padding.x;

            for (auto *child: *panel)
            {
                if (!child || !child->IsVisible())
                    continue;

                Vector4f child_rect = child->GetDesiredRect();
                f32 child_width = child_rect.z;
                f32 child_height = child_rect.w;

                Vector4f new_rect;

                // Fill mode 处理高度
                switch (_fill_mode)
                {
                    case ELayoutFillMode::kKeepSelf:
                        // 高度保持原样
                        break;

                    case ELayoutFillMode::kFitParent:
                        child_height = ltwh.w - _padding.y - _padding.w;
                        break;

                    case ELayoutFillMode::kPercentParent:
                        child_height = (ltwh.w - _padding.y - _padding.w) * _percent;
                        break;

                    default:
                        AL_ASSERT(false);
                        break;
                }

                // 对齐控制（垂直方向）
                if (_align == EAlignment::kTop)
                {
                    new_rect.y = ltwh.y + _padding.y;
                }
                else if (_align == EAlignment::kCenter)
                {
                    f32 content_height = ltwh.w - _padding.y - _padding.w;
                    new_rect.y = ltwh.y + _padding.y + (content_height - child_height) * 0.5f;
                }
                else if (_align == EAlignment::kBottom)
                {
                    new_rect.y = ltwh.y + ltwh.w - _padding.w - child_height;
                }

                new_rect.x = x_cursor;
                new_rect.z = child_width;
                new_rect.w = child_height;

                child->SetDesiredRect(new_rect.x, new_rect.y, new_rect.z, new_rect.w);

                x_cursor += child_width + _spacing;
            }
        }

        void AnchorLayout::UpdateLayout(Panel *panel)
        {
            if (!panel)
                return;

            Vector4f ltwh = panel->GetDesiredRect();// x, y, width, height

            for (auto *child: *panel)
            {
                if (!child || !child->IsVisible())
                    continue;

                Vector4f child_rect = child->GetDesiredRect();// width, height
                Vector4f new_rect = child_rect;
                const auto &rect = child->GetRect();// 获取锚点信息

                // 通过锚点计算子元素左上角坐标
                new_rect.x = ltwh.x + rect._anchor.x * (ltwh.z - child_rect.z);
                new_rect.y = ltwh.y + rect._anchor.y * (ltwh.w - child_rect.w);

                child->SetDesiredRect(new_rect.x, new_rect.y, new_rect.z, new_rect.w);
            }
        }

    }// namespace UI
}// namespace Ailu