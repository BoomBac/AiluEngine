#include "Editors/BlendSpaceAssetEditor.h"

#include "Animation/Clip.h"
#include "Assets/Asset.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"
#include "Render/Mesh.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/ObjectAssetDropdown.h"
#include "UI/UIRenderer.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

using namespace Ailu::UI;

namespace Ailu::Editor
{
    namespace
    {
        constexpr f32 kToolbarHeight = 28.0f;
        constexpr f32 kInputHeight = 22.0f;
        constexpr f32 kPlotMarginLeft = 42.0f;
        constexpr f32 kPlotMarginTop = 18.0f;
        constexpr f32 kPlotMarginRight = 16.0f;
        constexpr f32 kPlotMarginBottom = 30.0f;
        const Color kPanelColor = Color(0.16f, 0.17f, 0.19f, 1.0f);
        const Color kCenterColor = Color(0.10f, 0.11f, 0.12f, 1.0f);
        const Color kGridColor = Color(0.30f, 0.33f, 0.37f, 0.42f);
        const Color kAxisColor = Color(0.68f, 0.70f, 0.74f, 0.75f);
        const Color kTextColor = Color(0.76f, 0.77f, 0.80f, 1.0f);
        const Color kMutedTextColor = Color(0.55f, 0.57f, 0.60f, 1.0f);
        const Color kSampleColor = Color(0.20f, 0.58f, 0.92f, 1.0f);
        const Color kSelectedSampleColor = Color(0.98f, 0.70f, 0.22f, 1.0f);
        const Color kPreviewColor = Color(0.30f, 0.90f, 0.60f, 1.0f);

        void StyleText(UI::Text *text, Color color = kTextColor, f32 size = 11.0f)
        {
            text->_color = color;
            text->FontSize(size);
        }

        UI::InputBlock *AddInputToRow(UI::HorizontalBox *row, const String &value)
        {
            auto *input = row->AddChild<UI::InputBlock>(value);
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                    .Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
            return input;
        }

        f32 SafeRangeSize(f32 minimum, f32 maximum)
        {
            return std::max(maximum - minimum, 0.001f);
        }

        String FormatAxisValue(f32 value)
        {
            return std::format("{:.3f}", value);
        }

        bool IsShiftDown()
        {
            return Input::IsKeyDownAccurate(EKey::kSHIFT) || Input::IsKeyDownAccurate(EKey::kLSHIFT) ||
                   Input::IsKeyDownAccurate(EKey::kRSHIFT);
        }

        bool IsControlDown()
        {
            return Input::IsKeyDownAccurate(EKey::kCONTROL) || Input::IsKeyDownAccurate(EKey::kLCONTROL) ||
                   Input::IsKeyDownAccurate(EKey::kRCONTROL);
        }
    }

    BlendSpaceGraph::BlendSpaceGraph() : UIElement("BlendSpaceGraph")
    {
        SetWantsMouseEvents(true);
        SetInteractiveEnabled(true);
        OnMouseDown() += [this](UI::UIEvent &event) { HandleMouseDown(event); };
        OnMouseUp() += [this](UI::UIEvent &event) { HandleMouseUp(event); };
        OnMouseMove() += [this](UI::UIEvent &event) { HandleMouseMove(event); };
        OnMouseDoubleClick() += [this](UI::UIEvent &event) { HandleMouseDoubleClick(event); };
    }

    Vector2f BlendSpaceGraph::MeasureDesiredSize()
    {
        return Vector2f(600.0f, 380.0f);
    }

    void BlendSpaceGraph::SetAsset(BlendSpaceAsset *asset)
    {
        _asset = asset;
        if (_asset == nullptr || _selected_sample >= static_cast<i32>(_asset->Samples().size()))
            _selected_sample = -1;
        InvalidatePaint();
    }

    void BlendSpaceGraph::SetSelectedSample(i32 sample_index)
    {
        _selected_sample = sample_index;
        InvalidatePaint();
    }

    void BlendSpaceGraph::SetPreviewInput(Vector2f input)
    {
        _preview_input = input;
        InvalidatePaint();
    }

    Vector2f BlendSpaceGraph::GetDefaultSamplePosition() const
    {
        if (_asset == nullptr)
            return Vector2f::kZero;
        return {(_asset->XRange().x + _asset->XRange().y) * 0.5f,
                (_asset->YRange().x + _asset->YRange().y) * 0.5f};
    }

    Vector4f BlendSpaceGraph::GetPlotRect() const
    {
        const Vector4f rect = GetContentRect();
        return {rect.x + kPlotMarginLeft, rect.y + kPlotMarginTop,
                std::max(rect.z - kPlotMarginLeft - kPlotMarginRight, 1.0f),
                std::max(rect.w - kPlotMarginTop - kPlotMarginBottom, 1.0f)};
    }

    Vector2f BlendSpaceGraph::ValueToScreen(Vector2f value) const
    {
        const Vector4f plot = GetPlotRect();
        const Vector2f x_range = _asset != nullptr ? _asset->XRange() : Vector2f(0.0f, 1.0f);
        const Vector2f y_range = _asset != nullptr ? _asset->YRange() : Vector2f(0.0f, 1.0f);
        const f32 x = (value.x - x_range.x) / SafeRangeSize(x_range.x, x_range.y);
        const f32 y = (value.y - y_range.x) / SafeRangeSize(y_range.x, y_range.y);
        return {plot.x + std::clamp(x, 0.0f, 1.0f) * plot.z,
                plot.y + plot.w - std::clamp(y, 0.0f, 1.0f) * plot.w};
    }

    Vector2f BlendSpaceGraph::ScreenToValue(Vector2f screen_position) const
    {
        const Vector4f plot = GetPlotRect();
        const Vector2f x_range = _asset != nullptr ? _asset->XRange() : Vector2f(0.0f, 1.0f);
        const Vector2f y_range = _asset != nullptr ? _asset->YRange() : Vector2f(0.0f, 1.0f);
        const f32 x = std::clamp((screen_position.x - plot.x) / plot.z, 0.0f, 1.0f);
        const f32 y = std::clamp((plot.y + plot.w - screen_position.y) / plot.w, 0.0f, 1.0f);
        return {x_range.x + x * SafeRangeSize(x_range.x, x_range.y),
                y_range.x + y * SafeRangeSize(y_range.x, y_range.y)};
    }

    bool BlendSpaceGraph::IsSnapEnabled() const
    {
        return _asset != nullptr && _selected_sample >= 0 &&
               _selected_sample < static_cast<i32>(_asset->Samples().size()) &&
               !_asset->Samples()[_selected_sample]._clip.IsEmpty();
    }

    Vector2f BlendSpaceGraph::SnapToNearestSample(Vector2f position, i32 ignored_sample) const
    {
        if (_asset == nullptr || _asset->Samples().empty())
            return position;
        i32 nearest_sample = -1;
        f32 nearest_distance_squared = std::numeric_limits<f32>::max();
        for (u32 index = 0u; index < _asset->Samples().size(); ++index)
        {
            if (static_cast<i32>(index) == ignored_sample || _asset->Samples()[index]._clip.IsEmpty())
                continue;
            const Vector2f delta = _asset->Samples()[index]._position - position;
            const f32 distance_squared = delta.x * delta.x + delta.y * delta.y;
            if (distance_squared < nearest_distance_squared)
            {
                nearest_distance_squared = distance_squared;
                nearest_sample = static_cast<i32>(index);
            }
        }
        return nearest_sample >= 0 ? _asset->Samples()[nearest_sample]._position : position;
    }

    bool BlendSpaceGraph::HitPreviewInput(Vector2f screen_position) const
    {
        if (_asset == nullptr)
            return false;
        const Vector2f delta = screen_position - ValueToScreen(_preview_input);
        return delta.x * delta.x + delta.y * delta.y <= 12.0f * 12.0f;
    }

    i32 BlendSpaceGraph::HitSample(Vector2f screen_position) const
    {
        if (_asset == nullptr)
            return -1;
        i32 result = -1;
        f32 best_distance = 12.0f * 12.0f;
        for (u32 index = 0u; index < _asset->Samples().size(); ++index)
        {
            const Vector2f point = ValueToScreen(_asset->Samples()[index]._position);
            const Vector2f delta = screen_position - point;
            const f32 distance = delta.x * delta.x + delta.y * delta.y;
            if (distance <= best_distance)
            {
                best_distance = distance;
                result = static_cast<i32>(index);
            }
        }
        return result;
    }

    void BlendSpaceGraph::HandleMouseDown(UI::UIEvent &event)
    {
        if (_asset == nullptr)
            return;
        if (event._key_code == EKey::kLBUTTON)
        {
            if (HitPreviewInput(event._mouse_position))
            {
                _dragging_preview_input = true;
                event._is_handled = true;
                return;
            }
            const i32 sample_index = HitSample(event._mouse_position);
            if (_on_sample_selected)
                _on_sample_selected(sample_index);
            _dragged_sample = sample_index;
            event._is_handled = true;
        }
        else if (event._key_code == EKey::kRBUTTON)
        {
            event._is_handled = true;
        }
    }

    void BlendSpaceGraph::HandleMouseUp(UI::UIEvent &event)
    {
        if (event._key_code == EKey::kLBUTTON && _dragging_preview_input)
        {
            _dragging_preview_input = false;
            event._is_handled = true;
            return;
        }
        if (event._key_code == EKey::kLBUTTON && _dragged_sample >= 0)
        {
            if (_on_sample_move_finished)
                _on_sample_move_finished(_dragged_sample);
            _dragged_sample = -1;
            event._is_handled = true;
        }
    }

    void BlendSpaceGraph::HandleMouseMove(UI::UIEvent &event)
    {
        if (_asset == nullptr)
            return;

        if (_dragging_preview_input)
        {
            Vector2f preview_input = ScreenToValue(event._mouse_position);
            if (IsControlDown() && IsSnapEnabled())
                preview_input = SnapToNearestSample(preview_input);
            _preview_input = preview_input;
            if (_on_preview_input_changed)
                _on_preview_input_changed(preview_input);
            InvalidatePaint();
            event._is_handled = true;
            return;
        }
        if (_dragged_sample < 0 && IsShiftDown())
        {
            Vector2f preview_input = ScreenToValue(event._mouse_position);
            if (IsControlDown() && IsSnapEnabled())
                preview_input = SnapToNearestSample(preview_input);
            _preview_input = preview_input;
            if (_on_preview_input_changed)
                _on_preview_input_changed(preview_input);
            InvalidatePaint();
            event._is_handled = true;
            return;
        }
        if (_dragged_sample < 0 || _dragged_sample >= static_cast<i32>(_asset->Samples().size()))
            return;

        Vector2f position = ScreenToValue(event._mouse_position);
        if (IsControlDown() && IsSnapEnabled())
            position = SnapToNearestSample(position, _dragged_sample);
        _asset->Samples()[_dragged_sample]._position = position;
        if (_on_sample_moved)
            _on_sample_moved(_dragged_sample, position);
        InvalidatePaint();
        event._is_handled = true;
    }

    void BlendSpaceGraph::HandleMouseDoubleClick(UI::UIEvent &event)
    {
        if (_asset == nullptr || event._key_code != EKey::kLBUTTON || HitSample(event._mouse_position) >= 0)
            return;
        if (_on_empty_double_click)
            _on_empty_double_click(ScreenToValue(event._mouse_position));
        event._is_handled = true;
    }

    void BlendSpaceGraph::RenderImpl(UI::UIRenderer &renderer)
    {
        const Vector4f rect = GetContentRect();
        if (rect.z <= 0.0f || rect.w <= 0.0f)
            return;
        UI::UIBrush background;
        background._type = UI::EUIBrushType::kColor;
        background._tint = kCenterColor;
        renderer.DrawQuad(rect, background, -0.2f);

        const Vector4f plot = GetPlotRect();
        UI::UIBrush plot_background;
        plot_background._type = UI::EUIBrushType::kColor;
        plot_background._tint = Color(0.075f, 0.085f, 0.095f, 1.0f);
        renderer.DrawQuad(plot, plot_background, -0.15f);

        const Vector2f x_range = _asset != nullptr ? _asset->XRange() : Vector2f(0.0f, 1.0f);
        const Vector2f y_range = _asset != nullptr ? _asset->YRange() : Vector2f(0.0f, 1.0f);
        for (u32 index = 0u; index <= 10u; ++index)
        {
            const f32 t = static_cast<f32>(index) / 10.0f;
            const f32 x = plot.x + plot.z * t;
            const f32 y = plot.y + plot.w - plot.w * t;
            renderer.DrawLine({x, plot.y}, {x, plot.y + plot.w}, 1.0f, kGridColor, -0.1f);
            renderer.DrawLine({plot.x, y}, {plot.x + plot.z, y}, 1.0f, kGridColor, -0.1f);
            if (index == 0u || index == 5u || index == 10u)
            {
                renderer.DrawText(FormatAxisValue(x_range.x + SafeRangeSize(x_range.x, x_range.y) * t),
                                  {x - 12.0f, plot.y + plot.w + 7.0f}, 10.0f, kMutedTextColor);
                renderer.DrawText(FormatAxisValue(y_range.x + SafeRangeSize(y_range.x, y_range.y) * t),
                                  {plot.x - 38.0f, y - 6.0f}, 10.0f, kMutedTextColor);
            }
        }
        renderer.DrawBox(Vector2f(plot.x, plot.y), Vector2f(plot.z, plot.w), 1.0f, kAxisColor, -0.05f);
        renderer.DrawText(_asset != nullptr && _asset->Is2D() ? "Y Axis" : "Value",
                          {plot.x + plot.z - 42.0f, rect.y + 2.0f}, 10.0f, kMutedTextColor);
        renderer.DrawText("X Axis", {plot.x + plot.z - 34.0f, plot.y + plot.w + 19.0f}, 10.0f, kMutedTextColor);

        if (_asset == nullptr)
            return;
        if (!_asset->Is2D() && _asset->Samples().size() > 1u)
        {
            for (u32 index = 1u; index < _asset->Samples().size(); ++index)
                renderer.DrawLine(ValueToScreen(_asset->Samples()[index - 1u]._position),
                                  ValueToScreen(_asset->Samples()[index]._position), 1.5f,
                                  Color(0.24f, 0.52f, 0.78f, 0.65f), 0.0f);
        }
        for (u32 index = 0u; index < _asset->Samples().size(); ++index)
        {
            const Vector2f point = ValueToScreen(_asset->Samples()[index]._position);
            const bool selected = static_cast<i32>(index) == _selected_sample;
            const Color color = selected ? kSelectedSampleColor : kSampleColor;
            renderer.DrawQuad({point.x - (selected ? 6.0f : 5.0f), point.y - (selected ? 6.0f : 5.0f),
                               selected ? 12.0f : 10.0f, selected ? 12.0f : 10.0f}, background, 0.2f);
            renderer.DrawBox(Vector2f(point.x - (selected ? 6.0f : 5.0f), point.y - (selected ? 6.0f : 5.0f)),
                             Vector2f(selected ? 12.0f : 10.0f, selected ? 12.0f : 10.0f), 1.5f, color, 0.25f);
            renderer.DrawText(std::format("{}", index + 1u), {point.x + 8.0f, point.y - 7.0f}, 10.0f, color);
        }
        const Vector2f preview_point = ValueToScreen(_preview_input);
        renderer.DrawLine({preview_point.x - 7.0f, preview_point.y}, {preview_point.x + 7.0f, preview_point.y},
                          1.5f, kPreviewColor, 0.3f);
        renderer.DrawLine({preview_point.x, preview_point.y - 7.0f}, {preview_point.x, preview_point.y + 7.0f},
                          1.5f, kPreviewColor, 0.3f);
        renderer.DrawText("Preview Input", {plot.x + 6.0f, plot.y + 6.0f}, 10.0f, kPreviewColor);
    }

    BlendSpaceAssetEditor::BlendSpaceAssetEditor()
        : AssetEditor("Blend Space Editor", Vector2f(1280.0f, 780.0f))
    {
        SetPosition(Vector2f(70.0f, 35.0f));
        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, kToolbarHeight));
        toolbar->SlotPadding() = UI::Padding(4.0f, 2.0f, 4.0f, 2.0f);
        AddAssetMenu(toolbar);
        auto add_button = [toolbar](const String &text, f32 width)
        {
            auto *button = toolbar->AddChild<UI::Button>(text);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(width, 0.0f)).Margin(Vector4f(0.0f, 0.0f, 2.0f, 0.0f));
            return button;
        };
        auto *save_button = add_button("Save", 50.0f);
        save_button->OnMouseClick() += [this](UI::UIEvent &event) { Save(); event._is_handled = true; };
        auto *add_button_widget = add_button("+ Sample", 70.0f);
        add_button_widget->OnMouseClick() += [this](UI::UIEvent &event)
        {
            AddSampleAt(_graph != nullptr ? _graph->GetDefaultSamplePosition() : Vector2f::kZero);
            event._is_handled = true;
        };
        auto *remove_button = add_button("Remove", 64.0f);
        remove_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            RemoveSelectedSample();
            event._is_handled = true;
        };
        _txt_status = toolbar->AddChild<UI::Text>("Saved");
        StyleText(_txt_status, kMutedTextColor);
        _txt_status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .Margin(Vector4f(8.0f, 0.0f, 0.0f, 0.0f));

        auto *main = root->AddChild<UI::SplitView>();
        main->_is_horizontal = true;
        main->SetRatio(0.22f);
        main->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

        auto *settings_border = main->AddChild<UI::Border>();
        settings_border->_bg_color = kPanelColor;
        auto *settings_scroll = settings_border->AddChild<UI::ScrollView>();
        settings_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *settings = settings_scroll->AddChild<UI::VerticalBox>();
        settings->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        settings->SlotPadding() = UI::Padding(5.0f);
        AddSectionTitle(settings, "Blend Space");
        _input_name = AddTextInput(settings, "Name", "", [this](String value)
        {
            if (_blend_space != nullptr)
                _blend_space->Name(value);
            MarkDirty();
        });
        auto *mode_row = AddPropertyRow(settings, "2D Mode");
        _check_2d = mode_row->AddChild<UI::CheckBox>();
        _check_2d->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(28.0f, 0.0f));
        _check_2d->_on_click += [this](bool value)
        {
            if (_blend_space != nullptr)
                _blend_space->Is2D(value);
            MarkDirty();
            if (_graph != nullptr)
                _graph->InvalidatePaint();
        };
        AddSectionTitle(settings, "Axis Ranges");
        _input_x_min = AddFloatInput(settings, "X Min", 0.0f, [this](f32 value) { UpdateRange(true, true, value); });
        _input_x_max = AddFloatInput(settings, "X Max", 1.0f, [this](f32 value) { UpdateRange(true, false, value); });
        _input_y_min = AddFloatInput(settings, "Y Min", 0.0f, [this](f32 value) { UpdateRange(false, true, value); });
        _input_y_max = AddFloatInput(settings, "Y Max", 1.0f, [this](f32 value) { UpdateRange(false, false, value); });
        AddSectionTitle(settings, "Workflow");
        auto *help = settings->AddChild<UI::Text>(
            "Double-click to add a sample. Drag points to reposition them.\n"
            "Shift: preview follows mouse. Ctrl: snap to a clip.");
        StyleText(help, kMutedTextColor);
        help->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 40.0f)).Margin(Vector4f(4.0f, 2.0f, 4.0f, 2.0f));

        auto *work_area = main->AddChild<UI::SplitView>();
        work_area->_is_horizontal = true;
        work_area->SetRatio(0.72f);
        auto *graph_area = work_area->AddChild<UI::SplitView>();
        graph_area->_is_horizontal = false;
        graph_area->SetRatio(0.58f);

        auto *preview_border = graph_area->AddChild<UI::Border>();
        preview_border->_bg_color = kPanelColor;
        auto *preview = preview_border->AddChild<UI::VerticalBox>();
        preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        preview->SlotPadding() = UI::Padding(8.0f);
        AddSectionTitle(preview, "Preview");
        auto *preview_canvas = preview->AddChild<UI::Border>();
        preview_canvas->_bg_color = Color(0.06f, 0.07f, 0.08f, 1.0f);
        preview_canvas->_border_color = Color(0.30f, 0.34f, 0.40f, 1.0f);
        preview_canvas->Thickness(1.0f);
        preview_canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .Margin(Vector4f(0.0f, 0.0f, 0.0f, 4.0f));
        _preview_image = preview_canvas->AddChild<UI::Image>();
        _preview_image->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .Margin(Vector4f(3.0f, 3.0f, 3.0f, 3.0f));
        _preview_image->SetWantsMouseEvents(true);
        _preview_image->SetInteractiveEnabled(true);
        _preview_image->OnMouseDown() += [this](UI::UIEvent &event)
        {
            if (_preview_image == nullptr || _animation_preview == nullptr)
                return;
            const Vector4f rect = _preview_image->GetArrangeRect();
            if (event._key_code == EKey::kLBUTTON)
            {
                _animation_preview->BeginCameraDrag(event._mouse_position - rect.xy);
            }
            else if (event._key_code == EKey::kRBUTTON)
                _animation_preview->BeginCameraPan(event._mouse_position - rect.xy);
            event._is_handled = true;
        };
        _preview_image->OnMouseUp() += [this](UI::UIEvent &event)
        {
            if (_animation_preview == nullptr)
                return;
            if (event._key_code == EKey::kLBUTTON)
                _animation_preview->EndCameraDrag();
            else if (event._key_code == EKey::kRBUTTON)
                _animation_preview->EndCameraPan();
            event._is_handled = true;
        };
        _preview_image->OnMouseMove() += [this](UI::UIEvent &event)
        {
            if (_preview_image == nullptr || _animation_preview == nullptr)
                return;
            const Vector4f rect = _preview_image->GetArrangeRect();
            const Vector2f local_position = event._mouse_position - rect.xy;
            _animation_preview->DragCamera(local_position);
            _animation_preview->PanCamera(local_position);
            event._is_handled = true;
        };
        _preview_image->OnMouseScroll() += [this](UI::UIEvent &event)
        {
            if (_animation_preview != nullptr)
                _animation_preview->ZoomCamera(event._scroll_delta);
            event._is_handled = true;
        };
        auto *time_row = preview->AddChild<UI::HorizontalBox>();
        time_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 24.0f));
        _txt_preview_time = time_row->AddChild<UI::Text>("0.000 / 1.000 s");
        StyleText(_txt_preview_time, kMutedTextColor, 10.0f);
        _txt_preview_time->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(92.0f, 0.0f));
        _preview_time_slider = time_row->AddChild<UI::Slider>(0.0f, 1.0f, 0.0f);
        _preview_time_slider->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
                .Margin(Vector4f(4.0f, 2.0f, 4.0f, 2.0f));
        _preview_time_slider->_on_value_change += [this](f32 value) { SetPreviewTime(value, false); };

        auto *playback_row = preview->AddChild<UI::HorizontalBox>();
        playback_row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 26.0f));
        playback_row->SlotPadding() = UI::Padding(2.0f, 1.0f, 2.0f, 1.0f);
        auto add_playback_button = [playback_row](const String &text, f32 width)
        {
            auto *button = playback_row->AddChild<UI::Button>(text);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                    .Size(Vector2f(width, 0.0f)).Margin(Vector4f(1.0f, 0.0f, 1.0f, 0.0f));
            return button;
        };
        auto *reset_button = add_playback_button("|<", 32.0f);
        reset_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _is_preview_playing = false;
            SetPreviewTime(0.0f);
            RefreshPreviewControls();
            event._is_handled = true;
        };
        auto *step_back_button = add_playback_button("<", 28.0f);
        step_back_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            StepPreview(-1.0f);
            event._is_handled = true;
        };
        _preview_play_button = add_playback_button("Play", 52.0f);
        _preview_play_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            TogglePreviewPlayback();
            event._is_handled = true;
        };
        auto *step_forward_button = add_playback_button(">", 28.0f);
        step_forward_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            StepPreview(1.0f);
            event._is_handled = true;
        };
        auto *end_button = add_playback_button(" >|", 32.0f);
        end_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _is_preview_playing = false;
            SetPreviewTime(_preview_duration);
            RefreshPreviewControls();
            event._is_handled = true;
        };
        _preview_loop_button = add_playback_button("Loop", 48.0f);
        _preview_loop_button->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _preview_loop = !_preview_loop;
            RefreshPreviewControls();
            event._is_handled = true;
        };
        _txt_preview = preview->AddChild<UI::Text>(
            "Assign Animation Clips with a Preview Mesh to the Blend Space.");
        StyleText(_txt_preview, kMutedTextColor);
        _txt_preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 34.0f));

        auto *blend_border = graph_area->AddChild<UI::Border>();
        blend_border->_bg_color = kCenterColor;
        auto *blend = blend_border->AddChild<UI::VerticalBox>();
        blend->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        blend->SlotPadding() = UI::Padding(5.0f);
        AddSectionTitle(blend, "Blend Space");
        auto *graph_border = blend->AddChild<UI::Border>();
        graph_border->_bg_color = kCenterColor;
        graph_border->_border_color = Color(0.28f, 0.31f, 0.36f, 1.0f);
        graph_border->Thickness(1.0f);
        graph_border->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _graph = graph_border->AddChild<BlendSpaceGraph>();
        _graph->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _graph->SetSampleSelectedCallback([this](i32 index) { SelectSample(index); });
        _graph->SetSampleMovedCallback([this](i32 index, Vector2f position) { MoveSample(index, position); });
        _graph->SetSampleMoveFinishedCallback([this](i32 index) { FinishSampleMove(index); });
        _graph->SetEmptyDoubleClickCallback([this](Vector2f position) { AddSampleAt(position); });
        _graph->SetPreviewInputChangedCallback([this](Vector2f input) { SetPreviewInput(input); });
        _input_preview_x = AddFloatInput(blend, "Input X", 0.0f, [this](f32 value)
        {
            SetPreviewInput({value, _preview_input.y}, false);
        });
        _input_preview_y = AddFloatInput(blend, "Input Y", 0.0f, [this](f32 value)
        {
            SetPreviewInput({_preview_input.x, value}, false);
        });

        auto *details_border = work_area->AddChild<UI::Border>();
        details_border->_bg_color = kPanelColor;
        auto *details_scroll = details_border->AddChild<UI::ScrollView>();
        details_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *details = details_scroll->AddChild<UI::VerticalBox>();
        details->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        details->SlotPadding() = UI::Padding(5.0f);
        AddSectionTitle(details, "Samples");
        _samples_root = details->AddChild<UI::VerticalBox>();
        _samples_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        AddSectionTitle(details, "Selected Sample");
        _sample_details_root = details->AddChild<UI::VerticalBox>();
        _sample_details_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _animation_preview = MakeScope<AnimationClipPreview>();
    }

    UI::Text *BlendSpaceAssetEditor::AddSectionTitle(UI::UIElement *parent, const String &title)
    {
        auto *text = parent->AddChild<UI::Text>(title);
        StyleText(text, Color(0.88f, 0.89f, 0.92f, 1.0f), 13.0f);
        text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, 25.0f)).Margin(Vector4f(3.0f, 4.0f, 0.0f, 2.0f));
        return text;
    }

    UI::HorizontalBox *BlendSpaceAssetEditor::AddPropertyRow(UI::UIElement *parent, const String &label)
    {
        auto *row = parent->AddChild<UI::HorizontalBox>();
        row->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
        auto *text = row->AddChild<UI::Text>(label);
        StyleText(text, kMutedTextColor);
        text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size(Vector2f(76.0f, 0.0f));
        return row;
    }

    UI::InputBlock *BlendSpaceAssetEditor::AddTextInput(UI::UIElement *parent, const String &label,
                                                         const String &value,
                                                         const std::function<void(String)> &on_changed)
    {
        auto *input = AddInputToRow(AddPropertyRow(parent, label), value);
        input->_on_content_changed += std::function<void(String)>(on_changed);
        return input;
    }

    UI::InputBlock *BlendSpaceAssetEditor::AddFloatInput(UI::UIElement *parent, const String &label, f32 value,
                                                          const std::function<void(f32)> &on_changed)
    {
        auto *input = AddTextInput(parent, label, FormatAxisValue(value), [on_changed](String text)
        {
            if (auto parsed = StringUtils::ParseFloat(text); parsed.has_value())
                on_changed(parsed.value());
        });
        return input;
    }

    void BlendSpaceAssetEditor::Update(f32 dt)
    {
        AssetEditor::Update(dt);
        if (_animation_preview != nullptr)
        {
            if (!Input::IsKeyDownAccurate(EKey::kLBUTTON))
                _animation_preview->EndCameraDrag();
            if (!Input::IsKeyDownAccurate(EKey::kRBUTTON))
                _animation_preview->EndCameraPan();
        }
        if (_blend_space == nullptr || _animation_preview == nullptr || _preview_image == nullptr)
            return;

        if (_preview_input_dirty)
        {
            const f32 preview_duration = GetPreviewDuration();
            const bool time_was_clamped = _preview_time > preview_duration;
            _preview_duration = preview_duration;
            if (time_was_clamped)
                _preview_time = _preview_duration;
            _animation_preview->SetBlendSpaceInput(_preview_input, !_is_preview_playing);
            if (time_was_clamped)
                _animation_preview->SetTime(_preview_time);
            RefreshPreviewControls();
            _preview_input_dirty = false;
        }
        if (_is_preview_playing)
        {
            f32 next_time = _preview_time + std::max(dt, 0.0f);
            if (next_time >= _preview_duration)
            {
                if (_preview_loop && _preview_duration > 0.0f)
                    next_time = std::fmod(next_time, _preview_duration);
                else
                {
                    next_time = _preview_duration;
                    _is_preview_playing = false;
                }
            }
            SetPreviewTime(next_time);
            RefreshPreviewControls();
        }

        const Vector4f preview_rect = _preview_image->GetArrangeRect();
        if (preview_rect.z > 2.0f && preview_rect.w > 2.0f &&
            _animation_preview->SetViewportSize(preview_rect.zw))
            RefreshPreview();
        if (_animation_preview->IsRenderPending())
        {
            _animation_preview->RenderIfPending();
            _preview_image->SetTexture(_animation_preview->GetRenderTexture());
            _preview_image->InvalidatePaint();
        }
    }

    void BlendSpaceAssetEditor::RefreshPreview()
    {
        if (_animation_preview == nullptr)
            return;

        i32 sample_index = _selected_sample;
        if (_blend_space == nullptr || sample_index < 0 ||
            sample_index >= static_cast<i32>(_blend_space->Samples().size()))
            sample_index = -1;

        Guid clip_guid = Guid::EmptyGuid();
        if (sample_index >= 0)
            clip_guid = _blend_space->Samples()[sample_index]._clip;
        if (_preview_clip_guid != clip_guid)
        {
            _preview_clip_guid = clip_guid;
            _preview_clip.reset();
            _preview_mesh_guid = Guid::EmptyGuid();
            _preview_mesh.reset();
        }
        if (!clip_guid.IsEmpty() && _preview_clip == nullptr)
        {
            _preview_clip = ResourceMgr::Get().GetRef<AnimationClip>(clip_guid);
            if (_preview_clip == nullptr)
                _preview_clip = ResourceMgr::Get().Load<AnimationClip>(clip_guid);
        }
        if (_preview_clip != nullptr)
        {
            const Guid mesh_guid = _preview_clip->PreviewMeshGuid();
            if (_preview_mesh_guid != mesh_guid)
            {
                _preview_mesh_guid = mesh_guid;
                _preview_mesh.reset();
            }
            if (!_preview_mesh_guid.IsEmpty() && _preview_mesh == nullptr)
            {
                _preview_mesh = ResourceMgr::Get().GetRef<Render::SkeletonMesh>(_preview_mesh_guid);
                if (_preview_mesh == nullptr)
                _preview_mesh = ResourceMgr::Get().Load<Render::SkeletonMesh>(_preview_mesh_guid);
            }
        }

        if (_preview_mesh == nullptr && _blend_space != nullptr)
        {
            for (const auto &sample : _blend_space->Samples())
            {
                if (sample._clip.IsEmpty())
                    continue;
                Ref<AnimationClip> clip = ResourceMgr::Get().GetRef<AnimationClip>(sample._clip);
                if (clip == nullptr)
                    clip = ResourceMgr::Get().Load<AnimationClip>(sample._clip);
                if (clip == nullptr || clip->PreviewMeshGuid().IsEmpty())
                    continue;
                _preview_mesh_guid = clip->PreviewMeshGuid();
                _preview_mesh = ResourceMgr::Get().GetRef<Render::SkeletonMesh>(_preview_mesh_guid);
                if (_preview_mesh == nullptr)
                    _preview_mesh = ResourceMgr::Get().Load<Render::SkeletonMesh>(_preview_mesh_guid);
                if (_preview_mesh != nullptr)
                    break;
            }
        }

        _preview_duration = GetPreviewDuration();
        _preview_time = 0.0f;
        _is_preview_playing = false;
        _preview_input_dirty = false;
        _animation_preview->SetMesh(_preview_mesh.get());
        _animation_preview->SetBlendSpace(_blend_space, _preview_input);
        _animation_preview->SetShowSkeleton(false);
        RefreshPreviewControls();
        if (_preview_image != nullptr)
        {
            _preview_image->SetTexture(_preview_mesh != nullptr ? _animation_preview->GetRenderTexture() : nullptr);
            _preview_image->_uv_rect = Vector4f(0.0f, 0.0f, 1.0f, 1.0f);
            _preview_image->InvalidatePaint();
        }
        if (_txt_preview == nullptr)
            return;
        if (_blend_space == nullptr)
        {
            _txt_preview->SetText("Open a Blend Space asset to preview it.");
        }
        else if (_preview_clip == nullptr && _preview_mesh == nullptr)
        {
            _txt_preview->SetText("Assign Animation Clips with a Preview Mesh to the Blend Space.");
        }
        else if (_preview_clip == nullptr)
        {
            _txt_preview->SetText("Blend Space preview\nShift + move over the graph to change Input X/Y.");
        }
        else if (_preview_mesh == nullptr)
        {
            _txt_preview->SetText(std::format(
                "Sample {}: assign a Preview Mesh in the Animation Clip editor.\nDrag to orbit, right-drag to pan, scroll to zoom.",
                sample_index + 1));
        }
        else
        {
            _txt_preview->SetText(std::format(
                "Blend Space input [{:.3f}, {:.3f}]\nSample {}: {}\nDrag to orbit, right-drag to pan, scroll to zoom.",
                _preview_input.x, _preview_input.y, sample_index + 1,
                GetSampleName(_blend_space->Samples()[sample_index], sample_index)));
        }
    }

    bool BlendSpaceAssetEditor::OnOpen()
    {
        _blend_space = GetAssetObject<BlendSpaceAsset>();
        _selected_sample = -1;
        _preview_clip_guid = Guid::EmptyGuid();
        _preview_mesh_guid = Guid::EmptyGuid();
        _preview_clip.reset();
        _preview_mesh.reset();
        if (_blend_space != nullptr)
            _preview_input = {(_blend_space->XRange().x + _blend_space->XRange().y) * 0.5f,
                              (_blend_space->YRange().x + _blend_space->YRange().y) * 0.5f};
        if (_animation_preview != nullptr)
        {
            _animation_preview->SetClip(nullptr);
            _animation_preview->SetMesh(nullptr);
        }
        RefreshAllUI();
        return _blend_space != nullptr;
    }

    void BlendSpaceAssetEditor::OnClose()
    {
        _blend_space = nullptr;
        _selected_sample = -1;
        _preview_clip.reset();
        _preview_mesh.reset();
        _preview_clip_guid = Guid::EmptyGuid();
        _preview_mesh_guid = Guid::EmptyGuid();
        if (_animation_preview != nullptr)
        {
            _animation_preview->SetClip(nullptr);
            _animation_preview->SetMesh(nullptr);
        }
    }

    void BlendSpaceAssetEditor::OnAssetReloaded()
    {
        _blend_space = GetAssetObject<BlendSpaceAsset>();
        _selected_sample = -1;
        _preview_clip_guid = Guid::EmptyGuid();
        _preview_mesh_guid = Guid::EmptyGuid();
        _preview_clip.reset();
        _preview_mesh.reset();
        if (_blend_space != nullptr)
            _preview_input = {(_blend_space->XRange().x + _blend_space->XRange().y) * 0.5f,
                              (_blend_space->YRange().x + _blend_space->YRange().y) * 0.5f};
        RefreshAllUI();
    }

    void BlendSpaceAssetEditor::OnBeforeSave()
    {
        SortSamples();
    }

    void BlendSpaceAssetEditor::OnAssetSaved()
    {
        RefreshAllUI();
    }

    void BlendSpaceAssetEditor::MarkDirty()
    {
        if (_is_refreshing_ui)
            return;
        if (Asset *asset = GetAsset(); asset != nullptr && !asset->IsDirty())
            asset->MarkModified();
        if (_txt_status != nullptr)
            _txt_status->SetText("Modified");
    }

    void BlendSpaceAssetEditor::SetPreviewInput(Vector2f input, bool refresh_inputs)
    {
        if (_blend_space != nullptr)
        {
            const Vector2f x_range = _blend_space->XRange();
            const Vector2f y_range = _blend_space->YRange();
            input.x = std::clamp(input.x, x_range.x, x_range.y);
            input.y = std::clamp(input.y, y_range.x, y_range.y);
        }
        _preview_input = input;
        if (refresh_inputs && _input_preview_x != nullptr)
            _input_preview_x->SetContent(FormatAxisValue(_preview_input.x), false);
        if (refresh_inputs && _input_preview_y != nullptr)
            _input_preview_y->SetContent(FormatAxisValue(_preview_input.y), false);
        if (_graph != nullptr)
            _graph->SetPreviewInput(_preview_input);
        _preview_input_dirty = true;
    }

    void BlendSpaceAssetEditor::SetPreviewTime(f32 time, bool refresh_slider)
    {
        const f32 next_time = std::clamp(time, 0.0f, std::max(_preview_duration, 0.001f));
        const bool changed = !NearbyEqual(next_time, _preview_time);
        _preview_time = next_time;
        if (refresh_slider && _preview_time_slider != nullptr)
            _preview_time_slider->SetValue(_preview_time, false);
        if (_txt_preview_time != nullptr)
            _txt_preview_time->SetText(std::format("{:.3f} / {:.3f} s", _preview_time, _preview_duration));
        if (changed && _animation_preview != nullptr)
            _animation_preview->SetTime(_preview_time);
    }

    f32 BlendSpaceAssetEditor::GetPreviewDuration() const
    {
        if (_blend_space != nullptr)
        {
            AnimationEvaluation evaluation;
            _blend_space->AddSamples(_preview_input, 0.0f, 1.0f, true, evaluation);
            f32 duration_sum = 0.0f;
            f32 weight_sum = 0.0f;
            for (u8 sample_index = 0u; sample_index < evaluation._sample_count; ++sample_index)
            {
                const auto &sample = evaluation._samples[sample_index];
                if (sample._weight <= 0.0f)
                    continue;
                Ref<AnimationClip> clip = ResourceMgr::Get().GetRef<AnimationClip>(sample._clip);
                if (clip == nullptr)
                    clip = ResourceMgr::Get().Load<AnimationClip>(sample._clip);
                if (clip == nullptr)
                    continue;
                duration_sum += clip->Duration() * sample._weight;
                weight_sum += sample._weight;
            }
            return std::max(weight_sum > 0.0f ? duration_sum / weight_sum : 0.0f, 0.001f);
        }

        return std::max(_preview_clip != nullptr ? _preview_clip->Duration() : 0.0f, 0.001f);
    }

    void BlendSpaceAssetEditor::RefreshPreviewControls()
    {
        if (_preview_time_slider != nullptr)
        {
            _preview_time_slider->_range = {0.0f, std::max(_preview_duration, 0.001f)};
            _preview_time_slider->SetValue(_preview_time, false);
        }
        if (_txt_preview_time != nullptr)
            _txt_preview_time->SetText(std::format("{:.3f} / {:.3f} s", _preview_time, _preview_duration));
        if (_preview_play_button != nullptr)
            _preview_play_button->SetText(_is_preview_playing ? "Pause" : "Play", false);
        if (_preview_loop_button != nullptr)
            _preview_loop_button->SetText(_preview_loop ? "Loop On" : "Loop Off", false);
    }

    void BlendSpaceAssetEditor::TogglePreviewPlayback()
    {
        if (_preview_duration <= 0.0f)
            return;
        if (!_is_preview_playing && _preview_time >= _preview_duration - 0.0001f)
            SetPreviewTime(0.0f);
        _is_preview_playing = !_is_preview_playing;
        RefreshPreviewControls();
    }

    void BlendSpaceAssetEditor::StepPreview(f32 direction)
    {
        _is_preview_playing = false;
        SetPreviewTime(_preview_time + direction * (1.0f / 30.0f));
        RefreshPreviewControls();
    }

    void BlendSpaceAssetEditor::AddSampleAt(Vector2f position)
    {
        if (_blend_space == nullptr)
            return;
        const Vector2f x_range = _blend_space->XRange();
        const Vector2f y_range = _blend_space->YRange();
        BlendSpaceSample sample;
        sample._position = {std::clamp(position.x, x_range.x, x_range.y),
                            std::clamp(position.y, y_range.x, y_range.y)};
        _blend_space->AddSample(sample);
        _selected_sample = -1;
        for (i32 index = static_cast<i32>(_blend_space->Samples().size()) - 1; index >= 0; --index)
        {
            if (_blend_space->Samples()[index]._clip.IsEmpty() &&
                _blend_space->Samples()[index]._position == sample._position)
            {
                _selected_sample = index;
                break;
            }
        }
        MarkDirty();
        RefreshAllUI();
    }

    void BlendSpaceAssetEditor::RemoveSelectedSample()
    {
        if (_blend_space == nullptr || _selected_sample < 0 ||
            _selected_sample >= static_cast<i32>(_blend_space->Samples().size()))
            return;
        _blend_space->Samples().erase(_blend_space->Samples().begin() + _selected_sample);
        _selected_sample = std::min(_selected_sample, static_cast<i32>(_blend_space->Samples().size()) - 1);
        MarkDirty();
        RefreshAllUI();
    }

    void BlendSpaceAssetEditor::SelectSample(i32 sample_index)
    {
        _selected_sample = sample_index;
        RefreshSampleDetails();
        RefreshPreview();
        if (_graph != nullptr)
            _graph->SetSelectedSample(_selected_sample);
    }

    void BlendSpaceAssetEditor::MoveSample(i32 sample_index, Vector2f position)
    {
        if (_blend_space == nullptr || sample_index < 0 ||
            sample_index >= static_cast<i32>(_blend_space->Samples().size()))
            return;
        _selected_sample = sample_index;
        _blend_space->Samples()[sample_index]._position = position;
        if (_input_sample_x != nullptr)
            _input_sample_x->SetContent(FormatAxisValue(position.x), false);
        if (_input_sample_y != nullptr)
            _input_sample_y->SetContent(FormatAxisValue(position.y), false);
        MarkDirty();
    }

    void BlendSpaceAssetEditor::FinishSampleMove(i32 sample_index)
    {
        if (_blend_space == nullptr || sample_index < 0 ||
            sample_index >= static_cast<i32>(_blend_space->Samples().size()))
            return;
        const BlendSpaceSample moved_sample = _blend_space->Samples()[sample_index];
        SortSamples();
        _selected_sample = -1;
        for (u32 index = 0u; index < _blend_space->Samples().size(); ++index)
        {
            const auto &sample = _blend_space->Samples()[index];
            if (sample._clip == moved_sample._clip && sample._position == moved_sample._position)
            {
                _selected_sample = static_cast<i32>(index);
                break;
            }
        }
        RefreshAllUI();
    }

    void BlendSpaceAssetEditor::UpdateRange(bool is_x_axis, bool is_minimum, f32 value)
    {
        if (_blend_space == nullptr)
            return;
        Vector2f range = is_x_axis ? _blend_space->XRange() : _blend_space->YRange();
        if (is_minimum)
            range.x = std::min(value, range.y - 0.001f);
        else
            range.y = std::max(value, range.x + 0.001f);
        if (is_x_axis)
            _blend_space->XRange(range);
        else
            _blend_space->YRange(range);
        MarkDirty();
        if (_graph != nullptr)
            _graph->InvalidatePaint();
    }

    String BlendSpaceAssetEditor::GetSampleName(const BlendSpaceSample &sample, u32 index) const
    {
        if (sample._clip.IsEmpty())
            return std::format("Sample {} (unassigned)", index + 1u);
        for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
        {
            Asset *asset = it->second.get();
            if (asset != nullptr && asset->GetGuid() == sample._clip)
                return asset->Name().empty() ? ToChar(asset->_asset_path) : asset->Name();
        }
        return std::format("Sample {} ({})", index + 1u, sample._clip.ToString());
    }

    void BlendSpaceAssetEditor::SortSamples()
    {
        if (_blend_space == nullptr)
            return;
        std::stable_sort(_blend_space->Samples().begin(), _blend_space->Samples().end(),
                         [](const BlendSpaceSample &lhs, const BlendSpaceSample &rhs)
                         { return lhs._position.x < rhs._position.x; });
    }

    void BlendSpaceAssetEditor::RefreshSamples()
    {
        if (_samples_root == nullptr)
            return;
        _samples_root->ClearChildren();
        if (_blend_space == nullptr || _blend_space->Samples().empty())
        {
            auto *empty = _samples_root->AddChild<UI::Text>("No samples. Double-click the graph to add one.");
            StyleText(empty, kMutedTextColor);
            return;
        }
        for (u32 index = 0u; index < _blend_space->Samples().size(); ++index)
        {
            const auto &sample = _blend_space->Samples()[index];
            auto *button = _samples_root->AddChild<UI::Button>(
                std::format("{}  [{:.2f}, {:.2f}]", GetSampleName(sample, index), sample._position.x,
                            sample._position.y));
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 1.0f, 0.0f, 1.0f));
            const i32 sample_index = static_cast<i32>(index);
            button->OnMouseClick() += [this, sample_index](UI::UIEvent &event)
            {
                SelectSample(sample_index);
                event._is_handled = true;
            };
        }
    }

    void BlendSpaceAssetEditor::RefreshSampleDetails()
    {
        if (_sample_details_root == nullptr)
            return;
        _sample_details_root->ClearChildren();
        _sample_clip_dropdown = nullptr;
        _input_sample_x = nullptr;
        _input_sample_y = nullptr;
        if (_blend_space == nullptr || _selected_sample < 0 ||
            _selected_sample >= static_cast<i32>(_blend_space->Samples().size()))
        {
            auto *empty = _sample_details_root->AddChild<UI::Text>(
                "Select a sample to edit its animation and position.");
            StyleText(empty, kMutedTextColor);
            return;
        }
        const i32 sample_index = _selected_sample;
        const auto &sample = _blend_space->Samples()[sample_index];
        auto *clip_row = AddPropertyRow(_sample_details_root, "Animation");
        _sample_clip_dropdown = clip_row->AddChild<UI::ObjectAssetDropdown>(AnimationClip::StaticType());
        _sample_clip_dropdown->SetObjectType(AnimationClip::StaticType());
        _sample_clip_dropdown->SetAllowNone(true);
        _sample_clip_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                .Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
        _sample_clip_dropdown->SetSelectedGuid(sample._clip, false);
        _sample_clip_dropdown->_on_object_asset_selected += [this, sample_index](Asset *, Object *, const Guid &guid)
        {
            if (_blend_space == nullptr || sample_index >= static_cast<i32>(_blend_space->Samples().size()))
                return;
            _blend_space->Samples()[sample_index]._clip = guid;
            MarkDirty();
            RefreshSamples();
            RefreshPreview();
        };
        _input_sample_x = AddFloatInput(_sample_details_root, "X Position", sample._position.x,
                                        [this, sample_index](f32 value)
        {
            if (_blend_space == nullptr || sample_index >= static_cast<i32>(_blend_space->Samples().size()))
                return;
            const Vector2f range = _blend_space->XRange();
            _blend_space->Samples()[sample_index]._position.x = std::clamp(value, range.x, range.y);
            MarkDirty();
            if (_graph != nullptr)
                _graph->InvalidatePaint();
        });
        _input_sample_y = AddFloatInput(_sample_details_root, "Y Position", sample._position.y,
                                        [this, sample_index](f32 value)
        {
            if (_blend_space == nullptr || sample_index >= static_cast<i32>(_blend_space->Samples().size()))
                return;
            const Vector2f range = _blend_space->YRange();
            _blend_space->Samples()[sample_index]._position.y = std::clamp(value, range.x, range.y);
            MarkDirty();
            if (_graph != nullptr)
                _graph->InvalidatePaint();
        });
        auto *remove = _sample_details_root->AddChild<UI::Button>("Remove Selected Sample");
        remove->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                .Size(Vector2f(0.0f, kInputHeight)).Margin(Vector4f(0.0f, 5.0f, 0.0f, 1.0f));
        remove->OnMouseClick() += [this](UI::UIEvent &event) { RemoveSelectedSample(); event._is_handled = true; };
    }

    void BlendSpaceAssetEditor::RefreshAllUI()
    {
        _is_refreshing_ui = true;
        if (_input_name != nullptr)
            _input_name->SetContent(_blend_space != nullptr ? _blend_space->Name() : String(), false);
        if (_input_x_min != nullptr)
            _input_x_min->SetContent(FormatAxisValue(_blend_space != nullptr ? _blend_space->XRange().x : 0.0f), false);
        if (_input_x_max != nullptr)
            _input_x_max->SetContent(FormatAxisValue(_blend_space != nullptr ? _blend_space->XRange().y : 1.0f), false);
        if (_input_y_min != nullptr)
            _input_y_min->SetContent(FormatAxisValue(_blend_space != nullptr ? _blend_space->YRange().x : 0.0f), false);
        if (_input_y_max != nullptr)
            _input_y_max->SetContent(FormatAxisValue(_blend_space != nullptr ? _blend_space->YRange().y : 1.0f), false);
        if (_check_2d != nullptr)
            _check_2d->SetChecked(_blend_space != nullptr && _blend_space->Is2D());
        if (_txt_status != nullptr)
            _txt_status->SetText(IsDirty() ? "Modified" : "Saved");
        if (_input_preview_x != nullptr)
            _input_preview_x->SetContent(FormatAxisValue(_preview_input.x), false);
        if (_input_preview_y != nullptr)
            _input_preview_y->SetContent(FormatAxisValue(_preview_input.y), false);
        if (_graph != nullptr)
        {
            _graph->SetAsset(_blend_space);
            _graph->SetSelectedSample(_selected_sample);
            _graph->SetPreviewInput(_preview_input);
        }
        RefreshPreview();
        RefreshSamples();
        RefreshSampleDetails();
        _is_refreshing_ui = false;
    }
}
