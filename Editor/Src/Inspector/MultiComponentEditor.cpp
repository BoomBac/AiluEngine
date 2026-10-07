#include "Inspector/MultiComponentEditor.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Framework/Common/Input.h"
#include "UI/ColorPicker.h"
#include "UI/UIFramework.h"
#include <cmath>
#include <charconv>
#include <cctype>

namespace Ailu::Editor
{
    namespace
    {
        class InspectorColorPicker final : public UI::ColorPicker
        {
        public:
            InspectorColorPicker(Color color, std::function<void()> finish)
                : UI::ColorPicker(color), _finish(std::move(finish)) {}
            ~InspectorColorPicker() override { _finish(); }
        private:
            std::function<void()> _finish;
        };

        bool EqualValue(const InspectorValue &lhs, const InspectorValue &rhs)
        {
            if (lhs.index() != rhs.index())
                return false;
            if (const auto *number = std::get_if<double>(&lhs))
                return std::abs(*number - std::get<double>(rhs)) <= 0.000001;
            return lhs == rhs;
        }

        bool IsGuidText(const String &text)
        {
            if (text.empty()) return true;
            if (text.size() != 36) return false;
            for (u32 index = 0; index < text.size(); ++index)
                if (index == 8 || index == 13 || index == 18 || index == 23)
                {
                    if (text[index] != '-') return false;
                }
                else if (!std::isxdigit(static_cast<unsigned char>(text[index]))) return false;
            return true;
        }

        String FormatValue(const InspectorValue &value)
        {
            return std::visit([](const auto &data) -> String
            {
                using TValue = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<TValue, String>)
                    return data;
                else if constexpr (std::is_same_v<TValue, Guid>)
                    return data.IsEmpty() ? String{} : data.ToString();
                else if constexpr (std::is_same_v<TValue, Vector4f>)
                    return FormatColorButtonText(data, true);
                else if constexpr (std::is_same_v<TValue, double>)
                    return std::format("{:.4f}", data);
                else
                    return std::format("{}", data);
            }, value);
        }
    }

    InspectorAggregate ReadInspectorAggregate(const InspectorField &field, InspectorScene &scene,
                                              const Vector<Guid> &guids)
    {
        InspectorAggregate aggregate;
        for (const auto &guid : guids)
        {
            const auto entity = scene.FindEntity(guid);
            const auto value = scene.IsValidEntity(entity) && field._read ? field._read(scene, entity) : std::nullopt;
            if (!value)
            {
                aggregate._available = false;
                continue;
            }
            if (!aggregate._value)
                aggregate._value = value;
            else
                aggregate._mixed |= !EqualValue(*aggregate._value, *value);
        }
        aggregate._available &= !guids.empty() && aggregate._value.has_value();
        return aggregate;
    }

    InspectorPropertyCommand::InspectorPropertyCommand(const Ref<InspectorScene> &scene, Vector<Guid> guids,
                                                       InspectorField field) : _scene(scene), _field(std::move(field))
    {
        if (scene == nullptr || !_field._capture)
            return;
        for (const auto &guid : guids)
        {
            const auto entity = scene->FindEntity(guid);
            if (scene->IsValidEntity(entity) && _field._read(*scene, entity))
                _targets.push_back({guid, _field._capture(*scene, entity), {}});
        }
    }

    bool InspectorPropertyCommand::Preview(const InspectorValue &value)
    {
        const auto scene = _scene.lock();
        if (scene == nullptr || !_field._write || _field._read_only)
            return false;
        if (const auto *number = std::get_if<double>(&value); number != nullptr && !std::isfinite(*number))
            return false;
        bool changed = false;
        for (auto &target : _targets)
        {
            const auto entity = scene->FindEntity(target._guid);
            if (!scene->IsValidEntity(entity))
                continue;
            const auto before = _field._read(*scene, entity);
            if (!before || before->index() != value.index() || (!_field._action && EqualValue(*before, value)))
                continue;
            _field._write(*scene, entity, value);
            target._after = _field._capture(*scene, entity);
            const auto after = _field._read(*scene, entity);
            changed |= _field._action || (after && !EqualValue(*before, *after));
        }
        _changed |= changed;
        if (changed && _field._persistent)
            scene->MarkEdited();
        return changed;
    }

    void InspectorPropertyCommand::Apply(bool undo)
    {
        const auto scene = _scene.lock();
        if (scene == nullptr || !_field._restore)
            return;
        bool applied = false;
        for (const auto &target : _targets)
        {
            const auto entity = scene->FindEntity(target._guid);
            const auto &state = undo ? target._before : target._after;
            if (target._after.has_value() && state.has_value() && scene->IsValidEntity(entity))
            {
                _field._restore(*scene, entity, state);
                applied = true;
            }
        }
        if (applied && _field._persistent)
            scene->MarkEdited();
    }
    void InspectorPropertyCommand::Execute()
    {
        if (_initially_applied)
            _initially_applied = false;
        else
            Apply(false);
    }
    void InspectorPropertyCommand::Undo() { Apply(true); }

    MultiComponentEditor::~MultiComponentEditor()
    {
        _lifetime.reset();
        if (_command_manager != nullptr && _command_manager == g_pCommandMgr)
            _command_manager->BeforeHistoryChange().Unsubscribe(_history_subscription);
        FinishEdit();
    }

    InspectorScene *MultiComponentEditor::ResolveScene() const
    {
        if (const auto scene = _scene.lock())
            return scene.get();
        return _runtime_scene != nullptr && (!_context._is_current || _context._is_current()) ?
            _runtime_scene : nullptr;
    }

    void MultiComponentEditor::FinishEdit()
    {
        if (_pending_command != nullptr && _pending_command->HasChanges() && g_pCommandMgr != nullptr)
            g_pCommandMgr->ExecuteCommand(std::move(_pending_command));
        _pending_command.reset();
        _editing_binding = -1;
    }

    void MultiComponentEditor::Build(ComponentEditorContext &context)
    {
        FinishEdit();
        _context = context;
        if (context._scene == nullptr || context._content == nullptr)
            return;
        const auto owner = std::dynamic_pointer_cast<InspectorScene>(context._scene->SharedFromThis());
        _scene = owner;
        _runtime_scene = owner == nullptr ? context._scene : nullptr;
        _guids.clear();
        _bindings.clear();
        const auto entities = context._entities.empty() ? Vector<ECS::Entity>{context._entity} : context._entities;
        for (auto entity : entities)
            if (const auto *guid = context._scene->FindEntityGuid(entity))
                _guids.push_back(*guid);
        if (g_pCommandMgr != nullptr && _command_manager == nullptr)
        {
            _command_manager = g_pCommandMgr;
            _history_subscription = _command_manager->BeforeHistoryChange().Subscribe([this]()
            {
                UI::InputBlock *input = _editing_binding < 0 ? nullptr : _bindings[_editing_binding]._input;
                FinishEdit();
                if (input != nullptr && UI::UIManager::Get() != nullptr)
                    UI::UIManager::Get()->ClearFocus(input);
            });
        }
        for (auto &field : BuildComponentInspectorFields(context))
            if (ReadInspectorAggregate(field, *context._scene, _guids)._available)
                BuildField(std::move(field), context._content);
        _schema_signature = InspectorSchemaSignature(context);
        if (_bindings.empty())
            context._content->AddChild<UI::Text>("This component has no editable properties");
        Refresh(context);
    }

    void MultiComponentEditor::Write(u32 index, const InspectorValue &value, bool continuous)
    {
        if (_refreshing || index >= _bindings.size() || (_context._is_current && !_context._is_current()))
            return;
        auto *scene = ResolveScene();
        auto &field = _bindings[index]._field;
        if (scene == nullptr || field._read_only || !ReadInspectorAggregate(field, *scene, _guids)._available)
            return;
        if (const auto *number = std::get_if<double>(&value); number != nullptr && !std::isfinite(*number))
            return;
        if (_editing_binding != static_cast<i32>(index))
            FinishEdit();
        if (field._action && !field._persistent)
        {
            for (const auto &guid : _guids)
                field._write(*scene, scene->FindEntity(guid), value);
        }
        else if (const auto owner = _scene.lock())
        {
            if (_pending_command == nullptr)
            {
                _pending_command = MakeScope<InspectorPropertyCommand>(owner, _guids, field);
                _editing_binding = static_cast<i32>(index);
            }
            _pending_command->Preview(value);
        }
        else
        {
            for (const auto &guid : _guids)
                field._write(*scene, scene->FindEntity(guid), value);
            if (field._persistent)
                scene->MarkEdited();
        }
        if (!continuous || field._rebuild)
            FinishEdit();
        if (field._rebuild)
            _context.RequestRebuild();
        Refresh(_context);
    }

    void MultiComponentEditor::BuildField(InspectorField field, UI::VerticalBox *parent)
    {
        const u32 index = static_cast<u32>(_bindings.size());
        _bindings.push_back({std::move(field)});
        auto &binding = _bindings.back();
        const auto aggregate = ReadInspectorAggregate(binding._field, *_context._scene, _guids);
        const auto value = *aggregate._value;
        UI::HorizontalBox *value_box = nullptr;
        auto *row = AddPropertyRow(parent, binding._field._label, &value_box);
        row->Name(binding._field._key);
        const auto write = [this, weak = Weak<int>(_lifetime), index](const InspectorValue &value, bool continuous)
        {
            if (!weak.expired())
                Write(index, value, continuous);
        };
        if (binding._field._read_only)
            binding._text = value_box->AddChild<UI::Text>();
        else if (binding._field._action)
        {
            binding._button = value_box->AddChild<UI::Button>(binding._field._label);
            binding._button->OnMouseClick() += [write](UI::UIEvent &) { write(true, false); };
        }
        else if (binding._field._asset_type != nullptr)
        {
            auto *dropdown = value_box->AddChild<UI::ObjectAssetDropdown>(binding._field._asset_type);
            binding._dropdown = dropdown;
            dropdown->_on_object_asset_selected += [write](Asset *, Object *, const Guid &guid) { write(guid, false); };
        }
        else if (!binding._field._choices.empty())
        {
            binding._dropdown = value_box->AddChild<UI::Dropdown>(binding._field._choices);
            binding._dropdown->_on_selected_changed += [write, values = binding._field._choice_values,
                strings = binding._field._choice_strings, count = binding._field._choices.size()](i32 choice)
            {
                if (choice < 0 || static_cast<u32>(choice) >= count)
                    return;
                if (!strings.empty())
                    write(strings[choice], false);
                else
                    write(values.empty() ? static_cast<i64>(choice) : values[choice], false);
            };
        }
        else if (std::holds_alternative<bool>(value))
        {
            binding._checkbox = value_box->AddChild<UI::CheckBox>();
            binding._checkbox->_on_click += [write](bool checked) { write(checked, false); };
        }
        else if (std::holds_alternative<Vector4f>(value))
        {
            binding._button = value_box->AddChild<UI::Button>();
            binding._button->OnMouseClick() += [this, weak = Weak<int>(_lifetime), index, write](UI::UIEvent &event)
            {
                if (weak.expired() || ResolveScene() == nullptr)
                    return;
                const auto aggregate = ReadInspectorAggregate(_bindings[index]._field, *ResolveScene(), _guids);
                if (!aggregate._available || !aggregate._value)
                    return;
                FinishEdit();
                auto picker = MakeRef<InspectorColorPicker>(std::get<Vector4f>(*aggregate._value), [this, weak]
                    { if (!weak.expired()) FinishEdit(); });
                picker->GetSlot()->Size({320.0f, 240.0f});
                picker->OnValueChanged() += [write](Vector4f color) { write(color, true); };
                const auto rect = event._current_target->GetArrangeRect();
                UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, picker);
            };
        }
        else
        {
            auto *input = value_box->AddChild<UI::InputBlock>();
            binding._input = input;
            input->SetIntegerMode(std::holds_alternative<i64>(value));
            input->_on_content_changed += [write, type = value.index()](String content)
            {
                if (type == InspectorValue(i64{}).index())
                {
                    i64 number = 0;
                    const auto result = std::from_chars(content.data(), content.data() + content.size(), number);
                    if (result.ec == std::errc{} && result.ptr == content.data() + content.size())
                        write(number, true);
                }
                else if (type == InspectorValue(double{}).index())
                {
                    double number = 0;
                    const auto result = std::from_chars(content.data(), content.data() + content.size(), number);
                    if (result.ec == std::errc{} && result.ptr == content.data() + content.size() &&
                        std::isfinite(number))
                        write(number, true);
                }
                else if (type == InspectorValue(Guid{}).index())
                {
                    if (IsGuidText(content))
                        write(Guid(content), true);
                }
                else
                    write(content, true);
            };
            const auto finish = [this, weak = Weak<int>(_lifetime)] { if (!weak.expired()) FinishEdit(); };
            input->_on_focus_lost += [finish] { finish(); };
            input->OnMouseUp() += [input, finish](UI::UIEvent &) { if (!input->IsEditing()) finish(); };
            input->OnKeyDown() += [finish](UI::UIEvent &event) { if (event._key_code == EKey::kRETURN) finish(); };
        }
        for (const auto &child : value_box->GetChildren())
            child->GetSlotAs<UI::LinearSlot>().Margin(kPropInnerMargin)
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
    }

    void MultiComponentEditor::Refresh(ComponentEditorContext &)
    {
        auto *scene = ResolveScene();
        if (scene == nullptr)
            return;
        _refreshing = true;
        for (auto &binding : _bindings)
        {
            const auto aggregate = ReadInspectorAggregate(binding._field, *scene, _guids);
            if (!aggregate._value)
                continue;
            const auto &value = *aggregate._value;
            const String text = aggregate._mixed ? "Multiple values" : FormatValue(value);
            if (binding._text)
                binding._text->SetText(text);
            if (binding._input && !binding._input->IsEditing() && !binding._input->IsDragAdjusting())
                binding._input->SetContent(aggregate._mixed ? "--" : text, false);
            if (binding._checkbox)
            {
                binding._checkbox->SetChecked(std::get<bool>(value), false);
                binding._checkbox->SetMixed(aggregate._mixed);
            }
            if (binding._dropdown)
            {
                if (auto *asset = dynamic_cast<UI::ObjectAssetDropdown *>(binding._dropdown))
                    asset->SetSelectedGuid(std::get<Guid>(value), false);
                else
                {
                    i32 selected = -1;
                    if (!binding._field._choice_strings.empty())
                    {
                        const auto &strings = binding._field._choice_strings;
                        const auto it = std::find(strings.begin(), strings.end(), std::get<String>(value));
                        selected = it == strings.end() ? -1 : static_cast<i32>(std::distance(strings.begin(), it));
                    }
                    else if (!binding._field._choice_values.empty())
                    {
                        const auto &values = binding._field._choice_values;
                        const auto it = std::find(values.begin(), values.end(), std::get<i64>(value));
                        selected = it == values.end() ? -1 : static_cast<i32>(std::distance(values.begin(), it));
                    }
                    else
                        selected = static_cast<i32>(std::get<i64>(value));
                    binding._dropdown->SetSelectedIndex(selected, false);
                }
                binding._dropdown->SetMixed(aggregate._mixed);
            }
            if (binding._button && !binding._field._action)
                binding._button->SetText(text);
        }
        _refreshing = false;
    }

    bool MultiComponentEditor::NeedsRebuild(const ComponentEditorContext &context) const
    {
        return _schema_signature != InspectorSchemaSignature(context);
    }
}
