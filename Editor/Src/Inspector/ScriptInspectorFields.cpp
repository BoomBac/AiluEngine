#include "Inspector/ComponentInspectorFields.h"
#include "Assets/ScriptAsset.h"
#include "Framework/Script/ScriptSystem.h"
#include "Animation/Clip.h"
#include "Audio/AudioClip.h"
#include "Assets/PrefabAsset.h"
#include "Render/2D/Sprite.h"
#include "Render/Texture.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Animation/SkeletonAsset.h"

namespace Ailu::Editor
{
    namespace
    {
        ECS::ScriptPropertyData *FindProperty(ECS::ScriptComponent &component, const Guid &script,
                                              const String &name, ECS::EScriptPropertyType type,
                                              const String &asset_type)
        {
            if (component._script_asset != script)
                return nullptr;
            for (auto &property : component._properties)
                if (!property._is_orphan && property._name == name && property._type == type &&
                    (type != ECS::EScriptPropertyType::kAsset || property._asset_type == asset_type))
                    return &property;
            return nullptr;
        }

        const Type *AssetType(const String &name)
        {
            if (name == "Sprite") return Render::Sprite::StaticType();
            if (name == "Texture2D") return Render::Texture2D::StaticType();
            if (name == "Material") return Render::Material::StaticType();
            if (name == "Mesh") return Render::Mesh::StaticType();
            if (name == "SkeletonMesh") return Render::SkeletonMesh::StaticType();
            if (name == "AnimationClip") return AnimationClip::StaticType();
            if (name == "AudioClip") return AudioClip::StaticType();
            if (name == "Prefab") return PrefabAssetDocument::StaticType();
            if (name == "Script") return ScriptAsset::StaticType();
            return nullptr;
        }

        using ScriptState = std::pair<Guid, Vector<ECS::ScriptPropertyData>>;
        void RestoreScript(InspectorScene &scene, ECS::Entity entity, const ScriptState &state)
        {
            if (auto *component = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity))
            {
                ScriptSystem::Get().DestroyComponent(&scene, entity, *component);
                component->_script_asset = state.first;
                component->_properties = state.second;
            }
        }

        template<typename TValue, typename TRead, typename TWrite>
        InspectorField ScriptField(const ECS::ScriptComponent &component, const ECS::ScriptPropertyData &property,
                                   String suffix, String label, TRead read, TWrite write)
        {
            const auto find = [script = component._script_asset, name = property._name, type = property._type,
                               asset_type = property._asset_type](InspectorScene &scene, ECS::Entity entity)
                -> ECS::ScriptPropertyData *
            {
                auto *c = scene.GetRegister().GetComponent<ECS::ScriptComponent>(entity);
                return c ? FindProperty(*c, script, name, type, asset_type) : nullptr;
            };
            InspectorField field;
            field._key = std::format("script:{}:{}:{}:{}", component._script_asset.ToString(), property._name,
                static_cast<i32>(property._type), suffix);
            field._label = std::move(label);
            field._read = [find, read](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
                { auto *p = find(scene, entity);
                    return p ? std::optional(ToInspectorValue<TValue>(read(*p))) : std::nullopt; };
            field._write = [find, write](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
                { if (auto *p = find(scene, entity)) write(*p, FromInspectorValue<TValue>(value)); };
            field._capture = [find, read](InspectorScene &scene, ECS::Entity entity) -> std::any
                { auto *p = find(scene, entity); return p ? std::any(TValue(read(*p))) : std::any{}; };
            field._restore = [find, write](InspectorScene &scene, ECS::Entity entity, const std::any &state)
            {
                if (auto *p = find(scene, entity))
                    if (const auto *value = std::any_cast<TValue>(&state))
                        write(*p, *value);
            };
            return field;
        }
    }

    void BuildScriptInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields)
    {
        using T = ECS::ScriptComponent;
        auto asset = MakeInspectorField<T>("_script_asset", "Script Asset", [](T &c) { return c._script_asset; },
            [](T &, Guid) {});
        asset._asset_type = ScriptAsset::StaticType();
        asset._rebuild = true;
        asset._write = [](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
            { RestoreScript(scene, entity, {std::get<Guid>(value), {}}); };
        asset._capture = [](InspectorScene &scene, ECS::Entity entity) -> std::any
        {
            if (auto *c = scene.GetRegister().GetComponent<T>(entity))
                return ScriptState(c->_script_asset, c->_properties);
            return {};
        };
        asset._restore = [](InspectorScene &scene, ECS::Entity entity, const std::any &state)
            { if (const auto *value = std::any_cast<ScriptState>(&state)) RestoreScript(scene, entity, *value); };
        fields.push_back(std::move(asset));
        const auto entities = context._entities.empty() ? Vector<ECS::Entity>{context._entity} : context._entities;
        for (auto entity : entities)
            if (auto *c = context.Registry().GetComponent<T>(entity); c && !c->_script_asset.IsEmpty())
                ScriptSystem::Get().SynchronizeComponentProperties(*c);
        const auto *component = context.GetComponent<T>();
        if (component == nullptr)
            return;
        for (const auto &property : component->_properties)
        {
            if (property._is_orphan)
                continue;
#define SCRIPT_FIELD(value_type, path) \
            fields.push_back(ScriptField<value_type>(*component, property, "", property._name, \
                [](auto &p) { return p.path; }, [](auto &p, auto value) { p.path = value; }))
            switch (property._type)
            {
            case ECS::EScriptPropertyType::kBool: SCRIPT_FIELD(bool, _bool_value); break;
            case ECS::EScriptPropertyType::kInt: SCRIPT_FIELD(i32, _int_value); break;
            case ECS::EScriptPropertyType::kFloat: SCRIPT_FIELD(f32, _float_value); break;
            case ECS::EScriptPropertyType::kString: SCRIPT_FIELD(String, _string_value); break;
            case ECS::EScriptPropertyType::kAsset:
                SCRIPT_FIELD(Guid, _guid_value);
                fields.back()._asset_type = AssetType(property._asset_type);
                break;
            case ECS::EScriptPropertyType::kEntity: SCRIPT_FIELD(Guid, _guid_value); break;
            case ECS::EScriptPropertyType::kVector2:
            case ECS::EScriptPropertyType::kVector3:
            case ECS::EScriptPropertyType::kVector4:
            case ECS::EScriptPropertyType::kColor:
            {
                const u32 count = property._type == ECS::EScriptPropertyType::kVector2 ? 2 :
                    property._type == ECS::EScriptPropertyType::kVector3 ? 3 : 4;
                for (u32 axis = 0; axis < count; ++axis)
                    fields.push_back(ScriptField<f32>(*component, property, std::to_string(axis),
                        std::format("{} {}", property._name, "XYZW"[axis]),
                        [axis](auto &p) { return p._vector_value[axis]; },
                        [axis](auto &p, f32 value) { p._vector_value[axis] = value; }));
                break;
            }
            }
#undef SCRIPT_FIELD
        }
    }
}
