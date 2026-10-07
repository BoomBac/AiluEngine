#pragma once
#include "Inspector/ComponentEditorRegistry.h"
#include "Common/Undo.h"
#include <any>
#include <optional>
#include <variant>
#include <algorithm>
#include <limits>

namespace Ailu::UI { class InputBlock; class CheckBox; class Dropdown; class Button; class Text; }

namespace Ailu::Editor
{
    using InspectorValue = std::variant<bool, i64, double, String, Guid, Vector4f>;
    using InspectorScene = SceneManagement::Scene;

    struct InspectorField
    {
        String _key;
        String _label;
        Vector<String> _choices;
        Vector<i64> _choice_values;
        Vector<String> _choice_strings;
        const Type *_asset_type = nullptr;
        bool _read_only = false;
        bool _persistent = true;
        bool _rebuild = false;
        bool _action = false;
        std::optional<Vector2f> _range;
        std::function<std::optional<InspectorValue>(InspectorScene &, ECS::Entity)> _read;
        std::function<void(InspectorScene &, ECS::Entity, const InspectorValue &)> _write;
        std::function<std::any(InspectorScene &, ECS::Entity)> _capture;
        std::function<void(InspectorScene &, ECS::Entity, const std::any &)> _restore;
    };

    // Bindings own values/functions, and resolve ECS storage at each access.
    template<typename TValue> InspectorValue ToInspectorValue(const TValue &value)
    {
        if constexpr (std::is_enum_v<TValue> || (std::is_integral_v<TValue> && !std::is_same_v<TValue, bool>))
            return static_cast<i64>(value);
        else if constexpr (std::is_floating_point_v<TValue>)
            return static_cast<double>(value);
        else if constexpr (std::is_same_v<TValue, Color>)
            return Vector4f(value);
        else
            return value;
    }

    template<typename TValue> TValue FromInspectorValue(const InspectorValue &value)
    {
        if constexpr (std::is_enum_v<TValue>)
            return static_cast<TValue>(std::get<i64>(value));
        else if constexpr (std::is_integral_v<TValue> && !std::is_same_v<TValue, bool>)
        {
            const i64 minimum = static_cast<i64>(std::numeric_limits<TValue>::lowest());
            const i64 maximum = static_cast<i64>(std::numeric_limits<TValue>::max());
            return static_cast<TValue>(std::clamp(std::get<i64>(value), minimum, maximum));
        }
        else if constexpr (std::is_floating_point_v<TValue>)
            return static_cast<TValue>(std::clamp(std::get<double>(value),
                static_cast<double>(std::numeric_limits<TValue>::lowest()),
                static_cast<double>(std::numeric_limits<TValue>::max())));
        else if constexpr (std::is_same_v<TValue, Color>)
            return Color(std::get<Vector4f>(value));
        else
            return std::get<TValue>(value);
    }

    template<typename TComponent, typename TValue, typename TRead, typename TWrite>
    InspectorField BindInspectorField(String key, String label, TRead read, TWrite write)
    {
        InspectorField field;
        field._key = std::move(key);
        field._label = std::move(label);
        field._read = [read](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
        {
            auto *component = scene.GetRegister().GetComponent<TComponent>(entity);
            return component == nullptr ? std::nullopt : std::optional(ToInspectorValue<TValue>(read(*component)));
        };
        field._write = [write](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
        {
            if (auto *component = scene.GetRegister().GetComponent<TComponent>(entity))
                write(*component, FromInspectorValue<TValue>(value));
        };
        field._capture = [read](InspectorScene &scene, ECS::Entity entity) -> std::any
        {
            auto *component = scene.GetRegister().GetComponent<TComponent>(entity);
            return component == nullptr ? std::any{} : std::any(TValue(read(*component)));
        };
        field._restore = [write](InspectorScene &scene, ECS::Entity entity, const std::any &state)
        {
            if (auto *component = scene.GetRegister().GetComponent<TComponent>(entity))
                if (const auto *value = std::any_cast<TValue>(&state))
                    write(*component, *value);
        };
        return field;
    }

    struct InspectorAggregate
    {
        std::optional<InspectorValue> _value;
        bool _mixed = false;
        bool _available = true;
    };

    InspectorAggregate ReadInspectorAggregate(const InspectorField &field, InspectorScene &scene,
                                              const Vector<Guid> &guids);
    Vector<InspectorField> BuildComponentInspectorFields(const ComponentEditorContext &context);
    u64 InspectorSchemaSignature(const ComponentEditorContext &context);

    class InspectorPropertyCommand final : public ICommand
    {
    public:
        InspectorPropertyCommand(const Ref<InspectorScene> &scene, Vector<Guid> guids, InspectorField field);
        bool Preview(const InspectorValue &value);
        bool HasChanges() const { return _changed; }
        void Execute() override;
        void Undo() override;
        const String &ToString() const override { return _field._label; }
    private:
        struct Target { Guid _guid; std::any _before; std::any _after; };
        void Apply(bool undo);
        Weak<InspectorScene> _scene;
        InspectorField _field;
        Vector<Target> _targets;
        bool _changed = false;
        bool _initially_applied = true;
    };

    class MultiComponentEditor final : public IComponentEditor
    {
    public:
        ~MultiComponentEditor() override;
        void Build(ComponentEditorContext &context) override;
        void Refresh(ComponentEditorContext &context) override;
        bool NeedsRebuild(const ComponentEditorContext &context) const override;
        bool SupportsMultiEdit() const override { return true; }
        void FinishEdit();
    private:
        struct Binding
        {
            InspectorField _field;
            UI::InputBlock *_input = nullptr;
            UI::CheckBox *_checkbox = nullptr;
            UI::Dropdown *_dropdown = nullptr;
            UI::Button *_button = nullptr;
            UI::Text *_text = nullptr;
        };
        void BuildField(InspectorField field, UI::VerticalBox *parent);
        void Write(u32 index, const InspectorValue &value, bool continuous);
        InspectorScene *ResolveScene() const;
        Vector<Binding> _bindings;
        Vector<Guid> _guids;
        Weak<InspectorScene> _scene;
        InspectorScene *_runtime_scene = nullptr;
        ComponentEditorContext _context;
        Scope<InspectorPropertyCommand> _pending_command;
        i32 _editing_binding = -1;
        CommandManager *_command_manager = nullptr;
        u32 _history_subscription = 0;
        bool _refreshing = false;
        u64 _schema_signature = 0;
        Ref<int> _lifetime = MakeRef<int>(0);
    };
}
