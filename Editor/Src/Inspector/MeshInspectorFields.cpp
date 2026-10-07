#include "Inspector/ComponentInspectorFields.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"

namespace Ailu::Editor
{
    namespace
    {
        struct MaterialState
        {
            Ref<Render::Material> _material;
            InspectorValue _value;
        };
        struct MaterialTextureState
        {
            Ref<Render::Material> _material;
            Ref<Render::Texture> _texture;
        };

        template<typename TComponent>
        Ref<Render::Material> ResolveMaterial(InspectorScene &scene, ECS::Entity entity, u32 slot)
        {
            const auto *component = scene.GetRegister().GetComponent<TComponent>(entity);
            return component && slot < component->_p_mats.size() ? component->_p_mats[slot] : nullptr;
        }

        template<typename TComponent, typename TValue, typename TRead, typename TWrite>
        InspectorField MaterialField(u32 slot, String key, String label, TRead read, TWrite write)
        {
            InspectorField field;
            field._key = std::format("material:{}:{}", slot, key);
            field._label = std::format("Material[{}] {}", slot, label);
            field._persistent = false; // Writes mark the material asset, not just its referencing scene.
            field._read = [slot, read](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
            {
                auto material = ResolveMaterial<TComponent>(scene, entity, slot);
                return material ? read(*material) : std::nullopt;
            };
            field._write = [slot, write](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
            {
                if (auto material = ResolveMaterial<TComponent>(scene, entity, slot))
                {
                    write(*material, FromInspectorValue<TValue>(value));
                    ResourceMgr::Get().MarkAssetDirty(material.get());
                }
            };
            field._capture = [slot, read](InspectorScene &scene, ECS::Entity entity) -> std::any
            {
                auto material = ResolveMaterial<TComponent>(scene, entity, slot);
                const auto value = material ? read(*material) : std::nullopt;
                return value ? std::any(MaterialState{material, *value}) : std::any{};
            };
            field._restore = [write](InspectorScene &, ECS::Entity, const std::any &state)
            {
                if (const auto *value = std::any_cast<MaterialState>(&state); value && value->_material)
                {
                    write(*value->_material, FromInspectorValue<TValue>(value->_value));
                    ResourceMgr::Get().MarkAssetDirty(value->_material.get());
                }
            };
            return field;
        }

        template<typename TComponent>
        void MaterialFields(u32 slot, const Ref<Render::Material> &material, Vector<InspectorField> &fields)
        {
            if (material == nullptr)
                return;
            auto surface = MaterialField<TComponent, Render::ESurfaceType>(slot, "surface", "Surface Type",
                [](auto &m) -> std::optional<InspectorValue> { return ToInspectorValue(m.SurfaceType()); },
                [](auto &m, auto value) { m.SurfaceType(value); });
            surface._choices = {"Opaque", "Transparent", "Alpha Test"};
            surface._rebuild = true;
            fields.push_back(std::move(surface));
            auto cull = MaterialField<TComponent, Render::ECullMode>(slot, "cull", "Cull Mode",
                [](auto &m) -> std::optional<InspectorValue> { return ToInspectorValue(m.GetCullMode()); },
                [](auto &m, auto value) { m.SetCullMode(value); });
            cull._choices = {"Off", "Front", "Back"};
            fields.push_back(std::move(cull));
            for (const auto *property : material->GetShaderProperty())
            {
                const auto id = property->_property_id == Render::kInvalidShaderPropertyId
                    ? Render::ShaderPropertyRegistry::Get().Intern(property->_value_name) : property->_property_id;
                const auto type = property->_type;
                const auto resolve = [id, type](Render::Material &m) -> Render::ShaderPropertyInfo *
                {
                    auto *p = m.GetShaderProperty(id);
                    return p && p->_type == type ? p : nullptr;
                };
                if (type == Render::EShaderPropertyType::kFloat || type == Render::EShaderPropertyType::kRange)
                {
                    fields.push_back(MaterialField<TComponent, f32>(slot, property->_value_name, property->_prop_name,
                        [resolve](auto &m) -> std::optional<InspectorValue>
                        { auto *p = resolve(m); return p ? std::optional(ToInspectorValue(p->template GetValue<f32>()))
                                                        : std::nullopt; },
                        [resolve, id, type](auto &m, f32 value)
                        {
                            if (auto *p = resolve(m))
                            {
                                if (type == Render::EShaderPropertyType::kRange)
                                    value = std::clamp(value, p->_default_value.x, p->_default_value.y);
                                m.SetFloat(id, value);
                            }
                        }));
                    fields.back()._restore = [resolve, id](InspectorScene &, ECS::Entity, const std::any &state)
                    {
                        if (const auto *value = std::any_cast<MaterialState>(&state); value &&
                            value->_material && resolve(*value->_material))
                        {
                            value->_material->SetFloat(id, FromInspectorValue<f32>(value->_value));
                            ResourceMgr::Get().MarkAssetDirty(value->_material.get());
                        }
                    };
                }
                else if (type == Render::EShaderPropertyType::kVector)
                {
                    for (u32 axis = 0; axis < 4; ++axis)
                        fields.push_back(MaterialField<TComponent, f32>(slot,
                            std::format("{}[{}]", property->_value_name, axis),
                            std::format("{} {}", property->_prop_name, "XYZW"[axis]),
                            [resolve, axis](auto &m) -> std::optional<InspectorValue>
                            {
                                const auto *p = resolve(m);
                                return p ? std::optional(ToInspectorValue(p->template GetValue<Vector4f>()[axis]))
                                    : std::nullopt;
                            }, [resolve, id, axis](auto &m, f32 value)
                            {
                                if (auto *p = resolve(m))
                                {
                                    auto vector = p->template GetValue<Vector4f>();
                                    vector[axis] = value;
                                    m.SetVector(id, vector);
                                }
                            }));
                }
                else if (type == Render::EShaderPropertyType::kColor)
                {
                    fields.push_back(MaterialField<TComponent, Vector4f>(slot, property->_value_name,
                        property->_prop_name,
                        [resolve](auto &m) -> std::optional<InspectorValue>
                        { auto *p = resolve(m);
                            return p ? std::optional(InspectorValue(p->template GetValue<Vector4f>()))
                                                        : std::nullopt; },
                        [resolve, id](auto &m, Vector4f value) { if (resolve(m)) m.SetVector(id, value); }));
                }
                else if (type == Render::EShaderPropertyType::kTexture2D)
                {
                    auto texture = MaterialField<TComponent, Guid>(slot, property->_value_name, property->_prop_name,
                        [resolve](auto &m) -> std::optional<InspectorValue>
                        {
                            auto *p = resolve(m);
                            return p ? std::optional(InspectorValue(ResourceMgr::Get().GetAssetGuid(
                                reinterpret_cast<Render::Texture *>(p->_value_ptr)))) : std::nullopt;
                        }, [resolve, id](auto &m, Guid guid)
                        {
                            if (resolve(m))
                            {
                                auto texture = guid.IsEmpty() ? nullptr :
                                    ResourceMgr::Get().Load<Render::Texture2D>(guid);
                                m.SetTexture(id, texture.get());
                            }
                        });
                    texture._asset_type = Render::Texture2D::StaticType();
                    texture._capture = [slot, resolve](InspectorScene &scene, ECS::Entity entity) -> std::any
                    {
                        auto material = ResolveMaterial<TComponent>(scene, entity, slot);
                        auto *property = material ? resolve(*material) : nullptr;
                        if (!property) return {};
                        auto *texture = reinterpret_cast<Render::Texture *>(property->_value_ptr);
                        auto owner = texture ? std::dynamic_pointer_cast<Render::Texture>(texture->SharedFromThis())
                            : nullptr;
                        return MaterialTextureState{material, owner};
                    };
                    texture._restore = [resolve, id](InspectorScene &, ECS::Entity, const std::any &state)
                    {
                        if (const auto *value = std::any_cast<MaterialTextureState>(&state); value &&
                            value->_material && resolve(*value->_material))
                        {
                            value->_material->SetTexture(id, value->_texture.get());
                            ResourceMgr::Get().MarkAssetDirty(value->_material.get());
                        }
                    };
                    fields.push_back(std::move(texture));
                }
            }
        }

        template<typename TComponent, typename TMesh>
        void MeshFields(const ComponentEditorContext &context, Vector<InspectorField> &fields)
        {
            auto mesh = MakeInspectorField<TComponent>("_mesh_guid", "Mesh", [](auto &c)
                { return c._mesh_guid.IsEmpty() ? ResourceMgr::Get().GetAssetGuid(c._p_mesh.get()) : c._mesh_guid; },
                [](auto &c, Guid guid) { c._p_mesh = guid.IsEmpty() ? nullptr : ResourceMgr::Get().Load<TMesh>(guid);
                    c._mesh_guid = guid; });
            mesh._asset_type = TMesh::StaticType();
            mesh._rebuild = true;
            SetInspectorSnapshot<TComponent>(mesh, [](auto &c) { return std::pair(c._p_mesh, c._mesh_guid); },
                [](auto &c, const auto &state) { c._p_mesh = state.first; c._mesh_guid = state.second; });
            fields.push_back(std::move(mesh));
            auto motion = MakeInspectorField<TComponent>("motion_vector", "Motion Vector", [](auto &c)
                { return c._motion_vector_type; },
                [](auto &c, ECS::EMotionVectorType value) { c._motion_vector_type = value; });
            motion._choices = {"Camera Only", "Per Object", "Force Zero"};
            fields.push_back(std::move(motion));
            auto add = MakeInspectorField<TComponent>("add_material", "Add Material Slot", [](auto &) { return false; },
                [](auto &c, bool)
                {
                    const auto count = std::max(c._p_mats.size(), c._material_guids.size());
                    c._p_mats.resize(count);
                    c._material_guids.resize(count);
                    auto material = Render::Material::s_standard_defered_lit.lock();
                    c._p_mats.push_back(material);
                    c._material_guids.push_back(ResourceMgr::Get().GetAssetGuid(material.get()));
                });
            add._action = add._rebuild = true;
            SetInspectorSnapshot<TComponent>(add, [](auto &c) { return std::pair(c._p_mats, c._material_guids); },
                [](auto &c, const auto &state) { c._p_mats = state.first; c._material_guids = state.second; });
            fields.push_back(std::move(add));
            const auto *component = context.GetComponent<TComponent>();
            if (component == nullptr)
                return;
            const auto count = std::max(component->_p_mesh ? component->_p_mesh->SubmeshCount() : 0u,
                static_cast<u32>(std::max(component->_p_mats.size(), component->_material_guids.size())));
            for (u32 slot = 0; slot < count; ++slot)
            {
                auto material = MakeInspectorField<TComponent>(std::format("material:{}", slot),
                    std::format("Material[{}]", slot), [slot](auto &c)
                    {
                        if (slot < c._material_guids.size() && !c._material_guids[slot].IsEmpty())
                            return c._material_guids[slot];
                        return ResourceMgr::Get().GetAssetGuid(slot < c._p_mats.size() ?
                            c._p_mats[slot].get() : nullptr);
                    }, [slot](auto &c, Guid guid)
                    {
                        if (c._p_mats.size() <= slot) c._p_mats.resize(slot + 1);
                        if (c._material_guids.size() <= slot) c._material_guids.resize(slot + 1);
                        c._p_mats[slot] = guid.IsEmpty() ? nullptr : ResourceMgr::Get().Load<Render::Material>(guid);
                        c._material_guids[slot] = guid;
                    });
                material._asset_type = Render::Material::StaticType();
                material._rebuild = true;
                auto read = material._read;
                material._read = [read, slot](InspectorScene &scene, ECS::Entity entity)
                    -> std::optional<InspectorValue>
                {
                    const auto *c = scene.GetRegister().GetComponent<TComponent>(entity);
                    const auto count = c ? std::max(c->_p_mesh ? c->_p_mesh->SubmeshCount() : 0u,
                        static_cast<u32>(std::max(c->_p_mats.size(), c->_material_guids.size()))) : 0u;
                    return slot < count ? read(scene, entity) : std::nullopt;
                };
                SetInspectorSnapshot<TComponent>(material,
                    [](auto &c) { return std::pair(c._p_mats, c._material_guids); },
                    [](auto &c, const auto &state) { c._p_mats = state.first; c._material_guids = state.second; });
                fields.push_back(std::move(material));
                MaterialFields<TComponent>(slot, slot < component->_p_mats.size() ?
                    component->_p_mats[slot] : nullptr, fields);
            }
        }
    }

    void BuildMeshInspectorFields(const ComponentEditorContext &context, Vector<InspectorField> &fields)
    {
        if (context._component_info->_component_type == ECS::CSkeletonMesh::StaticComponentTypeId())
            MeshFields<ECS::CSkeletonMesh, Render::SkeletonMesh>(context, fields);
        else
            MeshFields<ECS::StaticMeshComponent, Render::Mesh>(context, fields);
    }
}
