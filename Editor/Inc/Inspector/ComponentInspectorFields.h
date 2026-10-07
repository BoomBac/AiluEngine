#pragma once
#include "Inspector/MultiComponentEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Ailu::Editor
{
    inline void SetInspectorRange(InspectorField &field, double minimum, double maximum)
    {
        auto write = field._write;
        field._write = [write, minimum, maximum](InspectorScene &scene, ECS::Entity entity, InspectorValue value)
        {
            if (auto *number = std::get_if<double>(&value))
                *number = std::clamp(*number, minimum, maximum);
            else if (auto *number = std::get_if<i64>(&value))
                *number = std::clamp(*number, static_cast<i64>(minimum), static_cast<i64>(maximum));
            write(scene, entity, value);
        };
    }

    template<typename TComponent, typename TRead, typename TWrite>
    InspectorField MakeInspectorField(String key, String label, TRead read, TWrite write)
    {
        using TValue = std::decay_t<std::invoke_result_t<TRead, TComponent &>>;
        return BindInspectorField<TComponent, TValue>(std::move(key), std::move(label), read, write);
    }

    template<typename TComponent, typename TRead>
    InspectorField MakeReadOnlyInspectorField(String key, String label, TRead read)
    {
        auto field = MakeInspectorField<TComponent>(std::move(key), std::move(label), read,
            [](auto &, const auto &) {});
        field._read_only = true;
        return field;
    }

    template<typename TComponent, typename TRead, typename TWrite>
    void SetInspectorSnapshot(InspectorField &field, TRead read, TWrite write)
    {
        using TState = std::decay_t<std::invoke_result_t<TRead, TComponent &>>;
        field._capture = [read](InspectorScene &scene, ECS::Entity entity) -> std::any
        {
            if (auto *component = scene.GetRegister().GetComponent<TComponent>(entity))
                return TState(read(*component));
            return {};
        };
        field._restore = [write](InspectorScene &scene, ECS::Entity entity, const std::any &state)
        {
            if (auto *component = scene.GetRegister().GetComponent<TComponent>(entity))
                if (const auto *value = std::any_cast<TState>(&state))
                    write(*component, *value);
        };
    }

    void BuildReflectedInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields);
    void BuildScriptInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields);
    void BuildMeshInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields);
    void BuildCollider2DInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields);
}
