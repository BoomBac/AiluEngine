#include "Inspector/ComponentInspectorFields.h"
#include "Physics/2D/Physics2DComponents.h"

namespace Ailu::Editor
{
    namespace
    {
        using T = ECS::Collider2DComponent;
        using ProfileState = std::pair<ECS::ECollisionPreset2D, ECS::CollisionProfile2D>;
        void CaptureProfile(InspectorField &field)
        {
            SetInspectorSnapshot<T>(field, [](T &c) { return ProfileState(c._preset, c._collision_profile); },
                [](T &c, const ProfileState &state) { c._preset = state.first; c._collision_profile = state.second; });
        }

        template<typename TRead, typename TWrite>
        InspectorField ShapeField(u32 index, String name, TRead read, TWrite write)
        {
            auto field = MakeInspectorField<T>(std::format("shape:{}:{}", index, name),
                std::format("Shape {} {}", index, name), [index, read](T &c) { return read(c._shapes[index]); },
                [index, write](T &c, auto value) { if (index < c._shapes.size()) write(c._shapes[index], value); });
            auto reader = field._read;
            field._read = [reader, index](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
            {
                const auto *component = scene.GetRegister().GetComponent<T>(entity);
                return component && index < component->_shapes.size() ? reader(scene, entity) : std::nullopt;
            };
            // Restore the complete value array: element indices can disappear after shape actions.
            SetInspectorSnapshot<T>(field, [](T &c) { return c._shapes; },
                [](T &c, const auto &state) { c._shapes = state; });
            return field;
        }
    }

    void BuildCollider2DInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields)
    {
        auto preset = MakeInspectorField<T>("_preset", "Collision Preset", [](T &c) { return c._preset; },
            [](T &c, ECS::ECollisionPreset2D value) { c._preset = value;
                ECS::ApplyCollisionPreset2D(c._collision_profile, value); });
        preset._choices = {"Default", "Player", "Enemy", "WorldStatic", "WorldDynamic", "Projectile",
                           "Trigger", "Pickup", "Custom"};
        CaptureProfile(preset);
        fields.push_back(std::move(preset));
        auto object_type = MakeInspectorField<T>("object_type", "Object Type", [](T &c)
            { return c._collision_profile._object_type; }, [](T &c, ECS::ECollisionChannel2D value)
            { c._collision_profile._object_type = value; c._preset = ECS::ECollisionPreset2D::kCustom; });
        object_type._choices = {"WorldStatic", "WorldDynamic", "Player", "Enemy", "Projectile", "Trigger", "Pickup"};
        CaptureProfile(object_type);
        fields.push_back(std::move(object_type));
        for (u32 channel = 0; channel < static_cast<u32>(ECS::ECollisionChannel2D::kCount); ++channel)
        {
            auto response = MakeInspectorField<T>(std::format("response:{}", channel),
                fields[1]._choices[channel] + " Response", [channel](T &c)
                { return c._collision_profile.GetResponse(static_cast<ECS::ECollisionChannel2D>(channel)); },
                [channel](T &c, ECS::ECollisionResponse2D value)
                { c._collision_profile.SetResponse(static_cast<ECS::ECollisionChannel2D>(channel), value);
                    c._preset = ECS::ECollisionPreset2D::kCustom; });
            response._choices = {"Ignore", "Overlap", "Block"};
            CaptureProfile(response);
            fields.push_back(std::move(response));
        }
        auto add = MakeInspectorField<T>("add_shape", "Add Shape", [](T &) { return false; },
            [](T &c, bool) { c._shapes.push_back(ECS::ColliderShape2D{}); });
        add._action = add._rebuild = true;
        SetInspectorSnapshot<T>(add, [](T &c) { return c._shapes; },
            [](T &c, const auto &state) { c._shapes = state; });
        fields.push_back(std::move(add));
        const auto *component = context.GetComponent<T>();
        if (component == nullptr)
            return;
        for (u32 index = 0; index < component->_shapes.size(); ++index)
        {
            const auto conditioned = [&](bool radius, bool capsule)
            {
                auto read = fields.back()._read;
                fields.back()._read = [read, index, radius, capsule](InspectorScene &scene, ECS::Entity entity)
                    -> std::optional<InspectorValue>
                {
                    const auto *c = scene.GetRegister().GetComponent<T>(entity);
                    if (c == nullptr || index >= c->_shapes.size())
                        return std::nullopt;
                    const auto type = c->_shapes[index]._type;
                    const bool available = capsule ? type == ECS::ECollider2DShape::kCapsule :
                        radius ? type == ECS::ECollider2DShape::kCircle || type == ECS::ECollider2DShape::kCapsule :
                        type == ECS::ECollider2DShape::kBox;
                    return available ? read(scene, entity) : std::nullopt;
                };
            };
#define SHAPE_FIELD(label, member) \
            fields.push_back(ShapeField(index, label, [](auto &s) { return s.member; }, \
                [](auto &s, auto value) { s.member = value; }))
            SHAPE_FIELD("Type", _type);
            fields.back()._choices = {"Box", "Circle", "Capsule", "Polygon", "Chain"};
            fields.back()._rebuild = true;
            SHAPE_FIELD("Center X", _center.x);
            SHAPE_FIELD("Center Y", _center.y);
            SHAPE_FIELD("Rotation", _rotation);
            SHAPE_FIELD("Width", _size.x);
            conditioned(false, false);
            SHAPE_FIELD("Height", _size.y);
            conditioned(false, false);
            SHAPE_FIELD("Radius", _radius);
            conditioned(true, false);
            SHAPE_FIELD("Capsule Height", _height);
            conditioned(false, true);
            SHAPE_FIELD("Trigger", _is_trigger);
            SHAPE_FIELD("Density", _density);
            SHAPE_FIELD("Friction", _friction);
            SHAPE_FIELD("Restitution", _restitution);
            SHAPE_FIELD("Layer", _layer);
            fields.back()._write = [index](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
            {
                if (auto *c = scene.GetRegister().GetComponent<T>(entity); c && index < c->_shapes.size())
                    c->_shapes[index]._layer = static_cast<u8>(std::clamp(std::get<i64>(value), i64(0), i64(31)));
            };
            fields.push_back(ShapeField(index, "Remove", [](auto &) { return false; }, [](auto &, bool) {}));
            fields.back()._action = fields.back()._rebuild = true;
            fields.back()._write = [index](InspectorScene &scene, ECS::Entity entity, const InspectorValue &)
            {
                if (auto *c = scene.GetRegister().GetComponent<T>(entity); c && index < c->_shapes.size())
                    c->_shapes.erase(c->_shapes.begin() + index);
            };
#undef SHAPE_FIELD
        }
    }
}
