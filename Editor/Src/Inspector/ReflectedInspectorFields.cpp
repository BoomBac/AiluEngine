#include "Inspector/ComponentInspectorFields.h"
#include "Render/Texture.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Animation/SkeletonAsset.h"
#include "Assets/PrefabAsset.h"
#include "Render/Mesh.h"
#include "Render/Material.h"
#include "Render/2D/Sprite.h"

namespace Ailu::Editor
{
    namespace
    {
        using PropertyPath = Vector<const PropertyInfo *>;
        void *ResolveInstance(const ComponentEditorInfo *info, const PropertyPath &path,
                              InspectorScene &scene, ECS::Entity entity)
        {
            void *instance = info->_get_component(scene.GetRegister(), entity);
            if (instance == nullptr)
                return nullptr;
            for (u32 index = 0; index + 1 < path.size(); ++index)
                instance = &path[index]->Get<u8>(instance);
            return instance;
        }

        template<typename TValue>
        InspectorField ReflectedField(const ComponentEditorInfo *info, PropertyPath path, String key, String label)
        {
            const auto *property = path.back();
            InspectorField field;
            field._key = std::move(key);
            field._label = std::move(label);
            field._read = [info, path, property](InspectorScene &scene, ECS::Entity entity)
                -> std::optional<InspectorValue>
            {
                auto *instance = ResolveInstance(info, path, scene, entity);
                return instance ? std::optional(ToInspectorValue<TValue>(property->Get<TValue>(instance))) :
                    std::nullopt;
            };
            field._write = [info, path, property](InspectorScene &scene, ECS::Entity entity,
                const InspectorValue &value)
            {
                auto *instance = ResolveInstance(info, path, scene, entity);
                if (instance == nullptr)
                    return;
                auto data = FromInspectorValue<TValue>(value);
                if constexpr (std::is_arithmetic_v<TValue> && !std::is_same_v<TValue, bool>)
                    if (property->MetaInfo().GetBool("IsRange", false))
                        data = static_cast<TValue>(std::clamp(static_cast<double>(data),
                            static_cast<double>(property->MetaInfo().GetFloat("RangeMin", 0)),
                            static_cast<double>(property->MetaInfo().GetFloat("RangeMax", 1))));
                // Preserve reflection notifications used by existing generic Inspector fields.
                property->Set<TValue>(instance, data, PropertyInfo::EPropertyChangeSource::kUI);
            };
            field._capture = [read = field._read](InspectorScene &scene, ECS::Entity entity) -> std::any
                { const auto value = read(scene, entity); return value ? std::any(*value) : std::any{}; };
            field._restore = [info, path, property](InspectorScene &scene, ECS::Entity entity, const std::any &state)
            {
                auto *instance = ResolveInstance(info, path, scene, entity);
                if (const auto *value = std::any_cast<InspectorValue>(&state); instance && value)
                    property->Set<TValue>(instance, FromInspectorValue<TValue>(*value),
                        PropertyInfo::EPropertyChangeSource::kUI);
            };
            field._read_only = std::any_of(path.begin(), path.end(), [](const PropertyInfo *item)
                { return item->MetaInfo().GetBool("ReadOnly", false); });
            return field;
        }

        template<typename TVector>
        void ReflectedVectorFields(const ComponentEditorInfo *info, const PropertyPath &path, const String &key,
                                   const String &label, u32 count, Vector<InspectorField> &fields)
        {
            using TValue = std::decay_t<decltype(std::declval<TVector>()[0])>;
            const auto *property = path.back();
            for (u32 axis = 0; axis < count; ++axis)
            {
                InspectorField field;
                field._key = std::format("{}[{}]", key, axis);
                field._label = std::format("{} {}", label, "XYZW"[axis]);
                field._read = [info, path, property, axis](InspectorScene &scene, ECS::Entity entity)
                    -> std::optional<InspectorValue>
                {
                    auto *instance = ResolveInstance(info, path, scene, entity);
                    return instance ? std::optional(ToInspectorValue<TValue>(property->Get<TVector>(instance)[axis]))
                                    : std::nullopt;
                };
                field._write = [info, path, property, axis](InspectorScene &scene, ECS::Entity entity,
                                                          const InspectorValue &value)
                {
                    if (auto *instance = ResolveInstance(info, path, scene, entity))
                    {
                        auto vector = property->Get<TVector>(instance);
                        vector[axis] = FromInspectorValue<TValue>(value);
                        property->Set<TVector>(instance, vector, PropertyInfo::EPropertyChangeSource::kUI);
                    }
                };
                field._capture = [read = field._read](InspectorScene &scene, ECS::Entity entity) -> std::any
                    { const auto value = read(scene, entity); return value ? std::any(*value) : std::any{}; };
                field._restore = [write = field._write](InspectorScene &scene, ECS::Entity entity,
                    const std::any &state)
                    { if (const auto *value = std::any_cast<InspectorValue>(&state)) write(scene, entity, *value); };
                field._read_only = std::any_of(path.begin(), path.end(), [](const PropertyInfo *item)
                    { return item->MetaInfo().GetBool("ReadOnly", false); });
                fields.push_back(std::move(field));
            }
        }

        void AddProperties(const ComponentEditorInfo *info, const Type *type, PropertyPath path,
                           const String &prefix, Vector<InspectorField> &fields)
        {
            if (type == nullptr || path.size() > 8)
                return;
            for (const auto &property : type->GetProperties())
            {
                if (property.MetaInfo().GetBool("Hidden", false))
                    continue;
                auto property_path = path;
                property_path.push_back(&property);
                const auto *property_type = property.GetType();
                if (property_type == nullptr)
                    continue;
                const String key = prefix + property.Name();
                const String label = property.MetaInfo().GetString("DisplayName", key);
#define REFLECT_SCALAR(value_type) \
                if (property_type == StaticClass<value_type>()) \
                { fields.push_back(ReflectedField<value_type>(info, property_path, key, label)); continue; }
                REFLECT_SCALAR(bool)
                REFLECT_SCALAR(f32)
                REFLECT_SCALAR(i32)
                REFLECT_SCALAR(u32)
                REFLECT_SCALAR(String)
                REFLECT_SCALAR(Color)
                REFLECT_SCALAR(Guid)
#undef REFLECT_SCALAR
#define REFLECT_VECTOR(vector_type, count) \
                if (property_type == StaticClass<vector_type>()) \
                { ReflectedVectorFields<vector_type>(info, property_path, key, label, count, fields); continue; }
                REFLECT_VECTOR(Vector2f, 2)
                REFLECT_VECTOR(Vector3f, 3)
                REFLECT_VECTOR(Vector4f, 4)
                REFLECT_VECTOR(Vector2Int, 2)
                REFLECT_VECTOR(Vector3Int, 3)
                REFLECT_VECTOR(Vector4Int, 4)
#undef REFLECT_VECTOR
                if (const auto *enum_type = dynamic_cast<const Enum *>(property_type))
                {
                    InspectorField field;
                    switch (property_type->Size())
                    {
                    case 1: field = ReflectedField<u8>(info, property_path, key, label); break;
                    case 2: field = ReflectedField<u16>(info, property_path, key, label); break;
                    case 4: field = ReflectedField<u32>(info, property_path, key, label); break;
                    default: continue;
                    }
                    for (const auto *name : enum_type->GetEnumNames())
                    {
                        field._choices.push_back(*name);
                        field._choice_values.push_back(enum_type->GetIndexByName(*name));
                    }
                    fields.push_back(std::move(field));
                }
                else if (property.TypeName().find('*') == String::npos && property.TypeName().find('<') == String::npos)
                    AddProperties(info, property_type, property_path, key + ".", fields);
            }
        }
    }

    void BuildReflectedInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields)
    {
        AddProperties(context._component_info, context._component_info->_reflection_type, {}, {}, fields);
    }
}
