#pragma once
#include "Common/Undo.h"

namespace Ailu::Editor
{
    enum class ETransformProperty { kPosition, kRotation, kScale };

    // Own values and stable entity identities, never pointers into ECS component storage.
    class TransformPropertyCommand final : public ICommand
    {
    public:
        TransformPropertyCommand(const Ref<SceneManagement::Scene> &scene, const Vector<ECS::Entity> &entities,
                                 ETransformProperty property, u32 axis);
        void Preview(f32 value);
        bool HasChanges() const;
        void Execute() override;
        void Undo() override;
        const String &ToString() const override { return _name; }
        static Vector3f ReadValue(const ECS::TransformComponent &component, ETransformProperty property);
        static bool WriteValue(ECS::TransformComponent &component, ETransformProperty property, u32 axis, f32 value);

    private:
        struct Target
        {
            Guid _guid;
            Transform _before;
            Transform _after;
        };
        void Apply(bool undo);
        Weak<SceneManagement::Scene> _scene;
        Vector<Target> _targets;
        ETransformProperty _property;
        u32 _axis;
        String _name = "Inspector Transform";
    };
}
