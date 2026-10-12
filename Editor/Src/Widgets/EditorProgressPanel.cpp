#include "Widgets/EditorProgressPanel.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace Ailu::Editor
{
    namespace
    {
        void SetBackground(UI::Border &border, Color color, f32 corner_radius = 0.0f, bool backdrop_blur = false)
        {
            UI::UIBrush brush;
            brush._type = backdrop_blur ? UI::EUIBrushType::kBackdropBlur : UI::EUIBrushType::kColor;
            brush._tint = color;
            border.GetStyleOverride().SetBackground(brush);
            border.GetStyleOverride().SetBorderWidth(0.0f);
            border.GetStyleOverride().SetCornerRadius(corner_radius);
        }
    }
    EditorProgressPanel::EditorProgressPanel(EditorProgressService &service) : _service(service), _widget(MakeRef<UI::Widget>())
    {
        _widget->Name("EditorProgressPanel");
        auto root = MakeRef<UI::Canvas>();
        for (size_t index = 0; index < _rows.size(); ++index)
        {
            auto &row = _rows[index];
            row._root = root->AddChild<UI::Border>();
            SetBackground(*row._root, {0.08f, 0.09f, 0.11f, 0.72f}, 10.0f, true);
            auto *content = row._root->AddChild<UI::VerticalBox>();
            auto *header = content->AddChild<UI::HorizontalBox>();
            header->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed).Size({0, 54});
            row._text = header->AddChild<UI::Text>("");
            row._text->FontSize(12);
            row._text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill).Margin({8, 0, 4, 0});
            row._button = header->AddChild<UI::Button>("Cancel");
            row._button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFixed)
                .Size({66, 24}).Margin({0, 0, 8, 0});
            row._button->OnMouseClick() += [this, index](UI::UIEvent &event)
            {
                const auto &clicked_row = _rows[index];
                if (clicked_row._dismiss) _service.DismissTask(clicked_row._id);
                else _service.RequestCancel(clicked_row._id);
                event._is_handled = true;
            };
            auto *bar = content->AddChild<UI::Canvas>();
            UI::UIBrush track_brush;
            track_brush._type = UI::EUIBrushType::kColor;
            track_brush._tint = {0.16f, 0.17f, 0.19f, 1.0f};
            bar->GetStyleOverride().SetBackground(track_brush);
            bar->GetStyleOverride().SetCornerRadius(3.0f);
            bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size({0, 6}).Margin({8, 0, 8, 6});
            row._fill = bar->AddChild<UI::Border>();
            SetBackground(*row._fill, {0.2f, 0.6f, 0.9f, 1.0f}, 3.0f);
        }
        _overflow = root->AddChild<UI::Text>("");
        _widget->AddToWidget(root);
    }
    EditorProgressPanel::~EditorProgressPanel()
    {
        if (_registered) UI::UIManager::Get()->UnRegisterWidget(_widget.get());
    }
    void EditorProgressPanel::Update(f32 dt, Vector2f window_size, f32 toolbar_height, f32 status_height)
    {
        _service.Tick(dt);
        _service.GetSnapshots(_snapshots);
        _summary.clear();
        size_t active_count = 0;
        const ProgressSnapshot *primary = nullptr;
        for (const auto &snapshot : _snapshots)
        {
            if (snapshot._state == EProgressState::kRunning || snapshot._state == EProgressState::kCancelling)
            {
                ++active_count;
                if (!primary) primary = &snapshot;
            }
        }
        if (primary) _summary = std::format("{} tasks: {}", active_count, primary->_title.substr(0, 36));
        // Active work precedes retained terminal states; stable order keeps concurrent rows predictable.
        std::stable_partition(_snapshots.begin(), _snapshots.end(), [](const auto &snapshot)
        { return snapshot._state == EProgressState::kRunning || snapshot._state == EProgressState::kCancelling; });
        const f32 available_height = std::max(0.0f, window_size.y - toolbar_height - status_height - 24.0f);
        const size_t visible_count = std::min({size_t(3), _snapshots.size(), static_cast<size_t>(available_height / 72.0f)});
        auto *manager = UI::UIManager::Get();
        if (visible_count == 0 || window_size.x <= 24.0f)
        {
            if (_registered) manager->UnRegisterWidget(_widget.get());
            _registered = false;
            return;
        }
        if (!_registered) { manager->RegisterWidget(_widget); _registered = true; }
        const f32 width = std::max(0.0f, std::min(320.0f, window_size.x - 24.0f));
        const bool has_overflow = _snapshots.size() > visible_count;
        const f32 height = std::min(available_height, visible_count * 72.0f + (has_overflow ? 20.0f : 0.0f));
        const Vector2f size{width, height};
        const Vector2f position{std::max(0.0f, window_size.x - width - 12.0f), window_size.y - status_height - height - 12.0f};
        if (_position != position) { _widget->SetPosition(position); _position = position; }
        if (_size != size)
        {
            _widget->SetSize(size);
            _widget->Root()->Arrange(0, 0, size.x, size.y);
            _size = size;
        }
        _animation_time = std::fmod(_animation_time + std::max(0.0f, dt), 1.0f);
        for (size_t index = 0; index < _rows.size(); ++index)
        {
            auto &row = _rows[index];
            row._root->SetVisible(index < visible_count);
            if (index >= visible_count) continue;
            row._root->GetSlotAs<UI::CanvasSlot>().Position({0, index * 72.0f}).Size({width, 66});
            const auto &snapshot = _snapshots[index];
            row._id = snapshot._id;
            row._dismiss = snapshot._state == EProgressState::kFailed;
            const char *state = "Running";
            switch (snapshot._state)
            {
            case EProgressState::kCancelling: state = "Cancelling"; break;
            case EProgressState::kCompleted: state = "Completed"; break;
            case EProgressState::kFailed: state = "Failed"; break;
            case EProgressState::kCancelled: state = "Cancelled"; break;
            default: break;
            }
            const f32 ratio = snapshot._total ? static_cast<f32>(std::min(1.0, double(snapshot._completed) / double(snapshot._total))) : 0;
            const String progress = snapshot._total ? std::format(" {:.0f}%", ratio * 100) : String(" (indeterminate)");
            const String text = snapshot._title.substr(0, 30) + "\n" + state + progress + "\n" + snapshot._message.substr(0, 40);
            if (row._text->GetText() != text) row._text->SetText(text, false);
            const bool can_cancel = snapshot._can_cancel && snapshot._state == EProgressState::kRunning;
            row._button->SetVisible(row._dismiss || can_cancel);
            const String button_text = row._dismiss ? "Dismiss" : "Cancel";
            if (row._button->GetText() != button_text) row._button->SetText(button_text, false);
            const bool active = snapshot._state == EProgressState::kRunning || snapshot._state == EProgressState::kCancelling;
            const f32 track_width = std::max(0.0f, width - 16.0f);
            const f32 fill_width = track_width * (snapshot._total ? ratio : (active ? 0.25f : 1.0f));
            const f32 fill_offset = !snapshot._total && active ? track_width * 0.75f * _animation_time : 0;
            row._fill->GetSlotAs<UI::CanvasSlot>().Position({fill_offset, 0}).Size({fill_width, 6});
        }
        _overflow->SetVisible(has_overflow);
        if (has_overflow)
        {
            const String text = std::format("+{} tasks", _snapshots.size() - visible_count);
            if (_overflow->GetText() != text) _overflow->SetText(text, false);
            _overflow->GetSlotAs<UI::CanvasSlot>().Position({8, height - 20}).Size({width - 8, 20});
        }
        manager->BringToFrontSilently(_widget.get());
        manager->EnsurePopupWidgetsOnTop();
    }
}
