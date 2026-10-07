#include "Inspector/TransformPropertyCommand.h"
#include <cmath>

namespace Ailu::Editor
{
    Vector3f TransformPropertyCommand::ReadValue(const ECS::TransformComponent &component, ETransformProperty property)
    {
        if (property == ETransformProperty::kPosition)
            return component.GetLocalPosition();
        if (property == ETransformProperty::kScale)
            return component.GetLocalScale();
        return Quaternion::EulerAngles(component._local_transform._rotation);
    }

    bool TransformPropertyCommand::WriteValue(ECS::TransformComponent &component, ETransformProperty property,
                                              u32 axis, f32 value)
    {
        if (axis >= 3 || !std::isfinite(value))
            return false;
        auto vector = ReadValue(component, property);
        if (vector[axis] == value)
            return false;
        vector[axis] = value;
        bool changed = false;
        if (property == ETransformProperty::kPosition)
            changed = component.SetLocalPosition(vector);
        else if (property == ETransformProperty::kScale)
            changed = component.SetLocalScale(vector);
        else
            changed = component.SetLocalRotation(Quaternion::EulerAngles(vector));
        if (changed)
            component.ResetRenderInterpolation();
        return changed;
    }

    TransformPropertyCommand::TransformPropertyCommand(const Ref<SceneManagement::Scene> &scene,
        const Vector<ECS::Entity> &entities, ETransformProperty property, u32 axis)
        : _scene(scene), _property(property), _axis(axis)
    {
        if (scene == nullptr)
            return;
        for (auto entity : entities)
        {
            if (!scene->IsValidEntity(entity))
                continue;
            const auto *component = scene->GetRegister().GetComponent<ECS::TransformComponent>(entity);
            const auto *guid = scene->FindEntityGuid(entity);
            if (component != nullptr && guid != nullptr)
                _targets.push_back({*guid, component->_local_transform, component->_local_transform});
        }
    }

    void TransformPropertyCommand::Preview(f32 value)
    {
        auto scene = _scene.lock();
        if (scene == nullptr || _axis >= 3 || !std::isfinite(value))
            return;
        bool changed = false;
        for (auto &target : _targets)
        {
            const auto entity = scene->FindEntity(target._guid);
            if (!scene->IsValidEntity(entity))
                continue;
            auto *component = scene->GetRegister().GetComponent<ECS::TransformComponent>(entity);
            if (component == nullptr)
                continue;
            changed |= WriteValue(*component, _property, _axis, value);
            target._after = component->_local_transform;
        }
        if (changed)
            scene->MarkEdited();
    }

    bool TransformPropertyCommand::HasChanges() const
    {
        for (const auto &target : _targets)
        {
            if (target._before._position != target._after._position || target._before._scale != target._after._scale ||
                target._before._rotation != target._after._rotation)
                return true;
        }
        return false;
    }

    void TransformPropertyCommand::Apply(bool undo)
    {
        auto scene = _scene.lock();
        if (scene == nullptr)
            return;
        bool changed = false;
        for (const auto &target : _targets)
        {
            const auto entity = scene->FindEntity(target._guid);
            if (!scene->IsValidEntity(entity))
                continue;
            auto *component = scene->GetRegister().GetComponent<ECS::TransformComponent>(entity);
            if (component != nullptr && component->SetLocalTransform(undo ? target._before : target._after))
            {
                component->ResetRenderInterpolation();
                changed = true;
            }
        }
        if (changed)
            scene->MarkEdited();
    }

    void TransformPropertyCommand::Execute() { Apply(false); }
    void TransformPropertyCommand::Undo() { Apply(true); }
}
