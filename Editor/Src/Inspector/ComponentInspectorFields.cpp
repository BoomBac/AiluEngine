#include "Inspector/ComponentInspectorFields.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/Clip.h"
#include "Project/ProjectManager.h"
#include "Render/2D/Sprite.h"
#include "Render/Material.h"
#include "Render/StaticDiffuseProbeData.h"
#include "Render/Texture.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu::Editor
{
    namespace
    {
#define FIELD(label, path) \
        fields.push_back(MakeInspectorField<T>(#path, label, [](T &component) { return component.path; }, \
            [](T &component, auto value) { component.path = value; }))
#define RANGE(label, path, minimum, maximum) \
        FIELD(label, path); SetInspectorRange(fields.back(), minimum, maximum)
#define ENUM_FIELD(label, path, ...) FIELD(label, path); fields.back()._choices = {__VA_ARGS__}
#define ASSET_FIELD(label, path, type) FIELD(label, path); fields.back()._asset_type = type::StaticType()
#define AXES(label, path, count) \
        for (u32 axis = 0; axis < count; ++axis) \
            fields.push_back(MakeInspectorField<T>(std::format("{}[{}]", #path, axis), \
                std::format("{} {}", label, "XYZW"[axis]), \
                [axis](T &component) { return component.path[axis]; }, \
                [axis](T &component, auto value) { component.path[axis] = value; }))
#define AXES_RANGE(label, path, count, minimum, maximum) \
        for (u32 axis = 0; axis < count; ++axis) \
        { \
            fields.push_back(MakeInspectorField<T>(std::format("{}[{}]", #path, axis), \
                std::format("{} {}", label, "XYZW"[axis]), \
                [axis](T &component) { return component.path[axis]; }, \
                [axis](T &component, auto value) { component.path[axis] = value; })); \
            SetInspectorRange(fields.back(), minimum, maximum); \
        }

        void LightFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::LightComponent;
            ENUM_FIELD("Type", _type, "Directional", "Point", "Spot", "Area");
            fields.back()._rebuild = true;
            RANGE("Intensity", _light._intensity, 0, 50000);
            fields.push_back(MakeInspectorField<T>("color", "Color", [](T &component)
                { return Color(component._light._light_color.r, component._light._light_color.g,
                               component._light._light_color.b, 1); }, [](T &component, Color value)
                { component._light._light_color.r = value.r; component._light._light_color.g = value.g;
                  component._light._light_color.b = value.b; }));
            FIELD("Cast Shadow", _shadow._is_cast_shadow);
            RANGE("Constant Bias", _shadow._constant_bias, 0, 1);
            RANGE("Slope Bias", _shadow._slope_bias, 0, 1);
            // Type-conditioned fields must exist on every target; mark availability in the reader.
            const auto conditioned = [&fields](auto condition)
            {
                auto read = fields.back()._read;
                fields.back()._read = [read, condition](InspectorScene &scene, ECS::Entity entity)
                    -> std::optional<InspectorValue>
                {
                    const auto *component = scene.GetRegister().GetComponent<T>(entity);
                    return component && condition(*component) ? read(scene, entity) : std::nullopt;
                };
            };
            FIELD("Use Color Temperature", _light._use_color_temperature);
            fields.back()._rebuild = true;
            RANGE("Color Temperature (K)", _light._color_temperature_kelvin, 1000, 40000);
            conditioned([](const T &c) { return c._light._use_color_temperature; });
            RANGE("Angular Radius", _light._angular_radius, .01, 5);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kDirectional; });
            RANGE("Range", _light._range, 0, 500);
            conditioned([](const T &c) { return c._type != ECS::ELightType::kDirectional; });
            RANGE("Point Radius", _light._source_radius, 0, 1);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kPoint; });
            RANGE("Inner Angle", _light._spot_inner_angle, 0, 180);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kSpot; });
            RANGE("Outer Angle", _light._spot_outer_angle, 0, 180);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kSpot; });
            fields.push_back(MakeInspectorField<T>("area_shape", "Shape", [](T &c)
                { return static_cast<i32>(c._light._area_shape); }, [](T &c, i32 v)
                { c._light._area_shape = static_cast<ECS::EAreaLightShape>(std::clamp(v, 0, 1)); }));
            fields.back()._choices = {"Rectangle", "Disc"};
            fields.back()._rebuild = true;
            conditioned([](const T &c) { return c._type == ECS::ELightType::kArea; });
            FIELD("Two Side", _light._is_two_side);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kArea; });
            FIELD("Width", _light._area_width);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kArea && c._light._area_shape == ECS::EAreaLightShape::kRectangle; });
            FIELD("Height", _light._area_height);
            conditioned([](const T &c) { return c._type == ECS::ELightType::kArea && c._light._area_shape == ECS::EAreaLightShape::kRectangle; });
            RANGE("Disc Radius", _light._area_disc_radius, 0, 10);
            fields.back()._key = "disc_radius";
            conditioned([](const T &c) { return c._type == ECS::ELightType::kArea && c._light._area_shape == ECS::EAreaLightShape::kDisc; });
        }

        void CameraFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::CCamera;
            fields.push_back(MakeInspectorField<T>("type", "Type", [](T &c) { return c._camera.Type(); },
                [](T &c, Render::ECameraType value) { c._camera.Type(value); }));
            fields.back()._choices = {"Orthographic", "Perspective"};
            fields.back()._rebuild = true;
#define CAMERA_FIELD(key, label, getter, setter) \
            fields.push_back(MakeInspectorField<T>(key, label, [](T &c) { return c._camera.getter(); }, \
                [](T &c, f32 value) { c._camera.setter(value); }))
            CAMERA_FIELD("near", "Near", Near, Near);
            CAMERA_FIELD("far", "Far", Far, Far);
            CAMERA_FIELD("aspect", "Aspect", Aspect, Aspect);
            CAMERA_FIELD("size", "Size", Size, Size);
            auto size_read = fields.back()._read;
            fields.back()._read = [size_read](InspectorScene &scene, ECS::Entity entity)
                -> std::optional<InspectorValue>
            {
                const auto *c = scene.GetRegister().GetComponent<T>(entity);
                return c && c->_camera.Type() == Render::ECameraType::kOrthographic ?
                    size_read(scene, entity) : std::nullopt;
            };
            CAMERA_FIELD("fov", "Fov", FovH, FovH);
            auto fov_read = fields.back()._read;
            fields.back()._read = [fov_read](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
            {
                const auto *c = scene.GetRegister().GetComponent<T>(entity);
                return c && c->_camera.Type() == Render::ECameraType::kPerspective ?
                    fov_read(scene, entity) : std::nullopt;
            };
#undef CAMERA_FIELD
        }

        void EnvironmentFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::EnvironmentComponent;
            const auto conditioned = [&](ECS::EEnvironmentSource source)
            {
                auto read = fields.back()._read;
                fields.back()._read = [read, source](InspectorScene &scene, ECS::Entity entity)
                    -> std::optional<InspectorValue>
                {
                    const auto *c = scene.GetRegister().GetComponent<T>(entity);
                    return c && c->_source == source ? read(scene, entity) : std::nullopt;
                };
            };
            ENUM_FIELD("Source", _source, "Procedural Sky", "Texture");
            fields.back()._rebuild = true;
            ASSET_FIELD("Texture", _texture, Render::Texture);
            conditioned(ECS::EEnvironmentSource::kTexture);
            FIELD("Sky Color", _procedural_sky._sky_color);
            conditioned(ECS::EEnvironmentSource::kProceduralSky);
            FIELD("Ground Color", _procedural_sky._ground_color);
            conditioned(ECS::EEnvironmentSource::kProceduralSky);
            RANGE("Intensity", _intensity, 0, 8);
            RANGE("Rotation", _rotation, -6.283185, 6.283185);
            FIELD("Render Sky", _render_sky);
            FIELD("Enable IBL", _enable_ibl);
            ENUM_FIELD("Preview Texture", _src_type, "Original", "IBL Diffuse", "IBL Specular");
            RANGE("Preview Mip", _mipmap, 0, 12);
        }

        void AnimatorFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::AnimatorComponent;
            auto controller = MakeInspectorField<T>("_controller", "Controller", [](T &c) { return c._controller; },
                [](T &c, Guid value) { c._controller = value; if (!value.IsEmpty()) c._clip = Guid::EmptyGuid();
                    c._instance = kInvalidAnimationInstanceHandle; });
            controller._asset_type = AnimationControllerAsset::StaticType();
            auto clip = MakeInspectorField<T>("_clip", "Clip", [](T &c) { return c._clip; },
                [](T &c, Guid value) { c._clip = value; if (!value.IsEmpty()) c._controller = Guid::EmptyGuid();
                    c._instance = kInvalidAnimationInstanceHandle; });
            clip._asset_type = AnimationClip::StaticType();
            for (auto *field : {&controller, &clip})
                SetInspectorSnapshot<T>(*field, [](T &c) { return std::pair(c._controller, c._clip); },
                    [](T &c, const auto &state) { c._controller = state.first; c._clip = state.second;
                        c._instance = kInvalidAnimationInstanceHandle; });
            fields.push_back(std::move(controller));
            fields.push_back(std::move(clip));
            RANGE("Speed", _speed, 0, std::numeric_limits<f32>::max());
            FIELD("Play On Awake", _play_on_awake);
            ENUM_FIELD("Root Motion", _root_motion_mode, "Disabled", "Extract Only", "Apply", "In Place");
        }

        void SpriteFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::SpriteRendererComponent;
            auto sprite = MakeInspectorField<T>("_sprite_guid", "Sprite", [](T &c)
                { return c._sprite_guid.IsEmpty() ? ResourceMgr::Get().GetAssetGuid(c._sprite) : c._sprite_guid; },
                [](T &c, Guid guid) {
                    auto resource = guid.IsEmpty() ? nullptr : ResourceMgr::Get().Load<Render::Sprite>(guid);
                    c._sprite = resource.get(); c._sprite_guid = guid; });
            sprite._asset_type = Render::Sprite::StaticType();
            fields.push_back(std::move(sprite));
            auto material = MakeInspectorField<T>("_material_guid", "Material", [](T &c)
                { return c._material_guid.IsEmpty() ? ResourceMgr::Get().GetAssetGuid(c._material.get()) :
                    c._material_guid; },
                [](T &c, Guid guid) {
                    c._material = guid.IsEmpty() ? nullptr : ResourceMgr::Get().Load<Render::Material>(guid);
                    c._material_guid = guid; });
            material._asset_type = Render::Material::StaticType();
            fields.push_back(std::move(material));
            FIELD("Color", _color);
            FIELD("Sorting Layer", _sorting_layer);
            FIELD("Order In Layer", _order_in_layer);
            ENUM_FIELD("Blend Mode", _blend_mode, "Alpha", "Additive", "Multiply", "Opaque");
            FIELD("Flip X", _flip_x);
            FIELD("Flip Y", _flip_y);
            FIELD("Visible", _visible);
        }

        void PostprocessFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::PostprocessVolumeComponent;
            FIELD("Global", _is_global);
            FIELD("Priority", _priority);
            FIELD("Blend Weight", _blend_weight);
            FIELD("Blend Distance", _blend_distance);
            FIELD("Bloom", _settings._bloom._enabled);
            FIELD("Bloom Intensity", _settings._bloom._intensity);
            FIELD("Bloom Radius", _settings._bloom._radius);
            FIELD("Exposure", _settings._tone_mapping._exposure);
            FIELD("Gamma", _settings._tone_mapping._gamma);
            ENUM_FIELD("Tone Mapping", _settings._tone_mapping._mode, "None", "ACES", "AgX", "Standard");
            FIELD("Depth of Field Enabled", _settings._depth_of_field._enabled);
            RANGE("Maximum CoC Diameter (px)", _settings._depth_of_field._maximum_coc_diameter_px, 0, 64);
            RANGE("Aperture Blade Count", _settings._depth_of_field._aperture_blade_count, 3, 9);
        }

        void RigidBodyFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::CRigidBody;
            FIELD("Mass", _mass);
            AXES("Velocity", _velocity, 3);
            AXES("Force", _force, 3);
            AXES("Angular Velocity", _angular_velocity, 3);
            AXES("Torque", _torque, 3);
            FIELD("Inertia", _inertia);
        }

        void ColliderFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::CCollider;
            ENUM_FIELD("Type", _type, "Box", "Sphere", "Capsule");
            fields.back()._rebuild = true;
            FIELD("Is Trigger", _is_trigger);
            AXES("Center", _center, 3);
            for (u32 axis = 0; axis < 3; ++axis)
            {
                fields.push_back(MakeInspectorField<T>(std::format("size:{}", axis),
                    std::format("Size {}", "XYZ"[axis]), [axis](T &c) { return c._param[axis]; },
                    [axis](T &c, f32 value) { c._param[axis] = value; }));
                auto read = fields.back()._read;
                fields.back()._read = [read](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
                {
                    const auto *c = scene.GetRegister().GetComponent<T>(entity);
                    return c && c->_type == ECS::EColliderType::kBox ? read(scene, entity) : std::nullopt;
                };
            }
            for (u32 axis = 0; axis < 2; ++axis)
            {
                fields.push_back(MakeInspectorField<T>(axis == 0 ? "radius" : "height", axis == 0 ? "Radius" : "Height",
                    [axis](T &c) { return c._param[axis]; }, [axis](T &c, f32 value) { c._param[axis] = value; }));
                auto read = fields.back()._read;
                fields.back()._read = [read, axis](InspectorScene &scene, ECS::Entity entity)
                    -> std::optional<InspectorValue>
                {
                    const auto *c = scene.GetRegister().GetComponent<T>(entity);
                    return c && (c->_type == ECS::EColliderType::kCapsule ||
                        (axis == 0 && c->_type == ECS::EColliderType::kSphere)) ? read(scene, entity) : std::nullopt;
                };
            }
        }

        void TagFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::TagComponent;
            auto name = MakeInspectorField<T>("_name", "Name", [](T &c) { return c._name; }, [](T &, String) {});
            name._write = [](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
                { scene.RenameEntity(entity, std::get<String>(value)); };
            name._restore = [](InspectorScene &scene, ECS::Entity entity, const std::any &state)
                { if (const auto *value = std::any_cast<String>(&state)) scene.RenameEntity(entity, *value); };
            fields.push_back(std::move(name));
            FIELD("Tag", _tag);
            if (ProjectManager::Get().HasOpenedProject())
            {
                fields.back()._choices = ProjectManager::Get().CurrentProject().Settings()._tags;
                fields.back()._choice_strings = fields.back()._choices;
            }
            FIELD("Layer Mask", _layer_mask);
            if (ProjectManager::Get().HasOpenedProject())
            {
                fields.back()._choices = ProjectManager::Get().CurrentProject().Settings()._layers;
                for (u32 index = 0; index < fields.back()._choices.size() && index < 32; ++index)
                    fields.back()._choice_values.push_back(i64(1u << index));
            }
        }

        void LightProbeFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::CLightProbe;
            FIELD("Update Every Tick", _is_update_every_tick);
            FIELD("Update Probe", _is_dirty);
            fields.back()._persistent = false;
            fields.back()._action = true;
        }

        void ProbeGroupFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::CLightProbeGroup;
            AXES_RANGE("Probe Count", _probe_count, 3, 1, std::numeric_limits<i32>::max());
            AXES_RANGE("Size", _size, 3, .01, std::numeric_limits<f32>::max());
            ASSET_FIELD("Baked SH", _baked_data, Render::StaticDiffuseProbeData);
            FIELD("Draw Probes", _draw_probes);
            FIELD("Preview Cubemap", _preview_cubemap);
            fields.back()._persistent = false;
            FIELD("Bake Static SH", _bake_requested);
            fields.back()._action = true;
            fields.back()._persistent = false;
            FIELD("Bake + PIX Capture", _capture_bake_requested);
            fields.back()._action = true;
            fields.back()._persistent = false;
            fields.back()._write = [](InspectorScene &scene, ECS::Entity entity, const InspectorValue &)
            {
                if (auto *c = scene.GetRegister().GetComponent<T>(entity))
                    c->_bake_requested = c->_capture_bake_requested = true;
            };
            auto clear = MakeInspectorField<T>("clear_bake", "Clear Bake", [](T &) { return false; },
                [](T &c, bool) { c._baked_data = Guid::EmptyGuid(); c._bake_requested = false; });
            clear._action = true;
            SetInspectorSnapshot<T>(clear, [](T &c) { return c._baked_data; },
                [](T &c, Guid value) { c._baked_data = value; c._bake_requested = false; });
            fields.push_back(std::move(clear));
        }

        void DdgiFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::DDGIVolumeComponent;
            AXES_RANGE("Probe Count", _probe_count, 3, 1, std::numeric_limits<u32>::max());
            AXES_RANGE("Probe Spacing", _probe_spacing, 3, .001, std::numeric_limits<f32>::max());
            FIELD("Draw Probes", _draw_probes);
            FIELD("Draw Probe Rays", _draw_probe_rays);
            FIELD("Probe Relocation", _use_probe_relocation);
            FIELD("Relocation Debug", _draw_relocation_debug);
            RANGE("Backface Threshold", _relocation_backface_threshold, 0, 1);
            RANGE("Random Ray Backface Threshold", _random_ray_backface_threshold, 0, 1);
            RANGE("Min Frontface Ratio", _relocation_min_frontface_distance_ratio, 0, .5);
            RANGE("Max Ray Distance", _max_ray_distance, .001, std::numeric_limits<f32>::max());
            RANGE("Probe Radius", _debug_probe_radius, .001, std::numeric_limits<f32>::max());
            RANGE("Ray Length", _debug_ray_length, .001, std::numeric_limits<f32>::max());
            FIELD("Freeze Ray Rotation", _freeze_ray_rotation);
            ENUM_FIELD("Debug Mode", _debug_mode, "Radiance", "Distance", "Hit Type", "Ray Direction", "Irradiance",
                "Distance Mean", "Distance Variance", "Final Weight");
        }

        void SurfelFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::SurfelGIComponent;
            FIELD("Enabled", _enabled);
            FIELD("Debug Draw Surfels", _debug_draw_surfels);
            FIELD("Opaque Debug Disks", _debug_opaque_disks);
            FIELD("Debug Irradiance", _debug_show_irradiance);
            RANGE("Debug Irradiance Exposure", _debug_irradiance_exposure, 0, 100);
            ENUM_FIELD("Lighting Debug", _debug_lighting_mode, "Cascade / Status", "Irradiance",
                "Sample Count", "Update Age");
            fields.back()._persistent = false;
            FIELD("Debug Coverage", _debug_coverage);
            FIELD("Draw Screen Tile Grid", _debug_draw_tile_grid);
            fields.back()._persistent = false;
            FIELD("Manual Spawn", _manual_spawn);
            fields.back()._persistent = false;
            auto spawn = MakeInspectorField<T>("spawn_once", "Spawn Once", [](T &) { return false; },
                [](T &c, bool) { c._manual_spawn = true; c._spawn_once_requested = true; });
            spawn._persistent = false;
            spawn._action = true;
            fields.push_back(std::move(spawn));
            RANGE("Base Cell Size", _base_cell_size, .001, 10);
            RANGE("Cascade Count", _cascade_count, 1, 5);
            RANGE("Grid Resolution", _grid_resolution, 8, 128);
            RANGE("Target Pixel Radius", _target_pixel_radius, 1, 64);
            RANGE("Spawn Coverage Threshold", _spawn_coverage_threshold, 0, 1);
            RANGE("Spawn Spacing Scale", _spawn_spacing_scale, .5, 3);
            RANGE("Max Spawn Per Frame", _max_spawn_per_frame, 1, 4096);
            FIELD("Lighting Enabled", _lighting_enabled);
            RANGE("Rays Per Surfel", _rays_per_surfel, 1, 8);
            RANGE("Lighting Updates Per Frame", _max_lighting_updates_per_frame, 1, 4096);
            RANGE("Lighting Max Ray Distance", _lighting_max_ray_distance, .01, std::numeric_limits<f32>::max());
            RANGE("Spawn Normal Threshold", _spawn_normal_threshold, 0, 1);
            RANGE("Blue Noise Strength", _blue_noise_strength, 0, 1);
            RANGE("Max Surfel Count", _max_surfel_count, 1, std::numeric_limits<u32>::max());
            RANGE("Debug Cascade", _debug_cascade, -1, 4);
        }

        void VxgiFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::CVXGI;
            AXES_RANGE("Grid Count", _grid_num, 3, 1, std::numeric_limits<i32>::max());
            FIELD("Distance", _distance);
            AXES("Grid Size", _grid_size, 3);
            AXES("Center", _center, 3);
            AXES("Size", _size, 3);
            FIELD("Draw Grid", _is_draw_grid);
            FIELD("Draw Voxel", _is_draw_voxel);
            FIELD("Mip", _mipmap);
            FIELD("Max Distance", _max_distance);
            FIELD("Min Distance", _min_distance);
            FIELD("Diffuse Cone Angle", _diffuse_cone_angle);
        }

        void AudioFields(const ComponentEditorContext &, Vector<InspectorField> &fields)
        {
            using T = ECS::AudioSourceComponent;
            FIELD("Audio Event", _audio_event);
            FIELD("Play On Awake", _play_on_awake);
            FIELD("Loop", _loop);
            FIELD("Spatial", _spatial);
            RANGE("Volume", _volume, 0, 1);
            RANGE("Pitch", _pitch, .01, 10);
            RANGE("Min Distance", _min_distance, 0, std::numeric_limits<f32>::max());
            RANGE("Max Distance", _max_distance, 0, std::numeric_limits<f32>::max());
            RANGE("Priority", _priority, 0, 1);
        }

#undef FIELD
#undef RANGE
#undef ENUM_FIELD
#undef ASSET_FIELD
#undef AXES
#undef AXES_RANGE
    }

    u64 InspectorSchemaSignature(const ComponentEditorContext &context)
    {
        if (!context._scene || !context._component_info)
            return 0;
        u64 signature = 0;
        const auto hash = [&](u64 value) { signature ^= value + 0x9e3779b97f4a7c15ull +
            (signature << 6) + (signature >> 2); };
        const auto hash_string = [&](const String &value) { hash(std::hash<String>{}(value)); };
        const auto mesh_schema = [&](const auto &component)
        {
            hash(component._p_mesh ? component._p_mesh->SubmeshCount() : 0);
            hash(component._material_guids.size());
            hash(component._p_mats.size());
            for (const auto &material : component._p_mats)
            {
                hash(material != nullptr);
                if (!material)
                    continue;
                hash(material->ID());
                for (const auto *property : material->GetShaderProperty())
                    if (property)
                    {
                        hash_string(property->_value_name);
                        hash(static_cast<u64>(property->_type));
                    }
            }
        };
        auto &registry = context.Registry();
        const auto type = context._component_info->_component_type;
        const Vector<ECS::Entity> single = context._entities.empty() ? Vector<ECS::Entity>{context._entity} :
            Vector<ECS::Entity>{};
        const auto &entities = context._entities.empty() ? single : context._entities;
        for (auto entity : entities)
        {
            hash(context._component_info->_has_component(registry, entity));
#define SCHEMA(component, body) \
            if (type == ECS::component::StaticComponentTypeId()) \
                if (const auto *c = registry.GetComponent<ECS::component>(entity)) { body; }
            SCHEMA(LightComponent, hash(static_cast<u64>(c->_type));
                if (c->_type == ECS::ELightType::kArea) hash(static_cast<u64>(c->_light._area_shape)))
            SCHEMA(CCamera, hash(static_cast<u64>(c->_camera.Type())))
            SCHEMA(CCollider, hash(static_cast<u64>(c->_type)))
            SCHEMA(EnvironmentComponent, hash(static_cast<u64>(c->_source)))
            SCHEMA(Collider2DComponent, hash(c->_shapes.size());
                for (const auto &shape : c->_shapes) hash(static_cast<u64>(shape._type)))
            SCHEMA(StaticMeshComponent, mesh_schema(*c))
            SCHEMA(CSkeletonMesh, mesh_schema(*c))
            SCHEMA(ScriptComponent, hash_string(c->_script_asset.ToString());
                for (const auto &property : c->_properties)
                {
                    hash_string(property._name); hash_string(property._asset_type);
                    hash(static_cast<u64>(property._type)); hash(property._is_orphan);
                })
#undef SCHEMA
        }
        return signature;
    }

    static Vector<InspectorField> BuildComponentProperties(const ComponentEditorContext &context)
    {
        Vector<InspectorField> fields;
        if (context._scene == nullptr || context._component_info == nullptr)
            return fields;
        const auto type = context._component_info->_component_type;
#define COMPONENT_FIELDS(component, builder) \
        if (type == ECS::component::StaticComponentTypeId()) { builder(context, fields); return fields; }
        COMPONENT_FIELDS(LightComponent, LightFields)
        COMPONENT_FIELDS(CCamera, CameraFields)
        COMPONENT_FIELDS(EnvironmentComponent, EnvironmentFields)
        COMPONENT_FIELDS(AnimatorComponent, AnimatorFields)
        COMPONENT_FIELDS(SpriteRendererComponent, SpriteFields)
        COMPONENT_FIELDS(PostprocessVolumeComponent, PostprocessFields)
        COMPONENT_FIELDS(CRigidBody, RigidBodyFields)
        COMPONENT_FIELDS(CCollider, ColliderFields)
        COMPONENT_FIELDS(TagComponent, TagFields)
        COMPONENT_FIELDS(CLightProbe, LightProbeFields)
        COMPONENT_FIELDS(CLightProbeGroup, ProbeGroupFields)
        COMPONENT_FIELDS(DDGIVolumeComponent, DdgiFields)
        COMPONENT_FIELDS(SurfelGIComponent, SurfelFields)
        COMPONENT_FIELDS(CVXGI, VxgiFields)
        COMPONENT_FIELDS(AudioSourceComponent, AudioFields)
        COMPONENT_FIELDS(ScriptComponent, BuildScriptInspectorFields)
        COMPONENT_FIELDS(StaticMeshComponent, BuildMeshInspectorFields)
        COMPONENT_FIELDS(CSkeletonMesh, BuildMeshInspectorFields)
        COMPONENT_FIELDS(Collider2DComponent, BuildCollider2DInspectorFields)
#undef COMPONENT_FIELDS
        if (type == ECS::PersistentIdComponent::StaticComponentTypeId())
            fields.push_back(MakeReadOnlyInspectorField<ECS::PersistentIdComponent>("guid", "GUID",
                [](auto &component) { return component._guid; }));
        else if (type == ECS::CHierarchy::StaticComponentTypeId())
        {
            fields.push_back(MakeReadOnlyInspectorField<ECS::CHierarchy>("parent", "Parent",
                [](auto &component) { return static_cast<i64>(component._parent); }));
            fields.push_back(MakeReadOnlyInspectorField<ECS::CHierarchy>("children", "Children",
                [](auto &component) { return static_cast<i64>(component._children_num); }));
        }
        else
            BuildReflectedInspectorFields(context, fields);
        return fields;
    }

    Vector<InspectorField> BuildComponentInspectorFields(const ComponentEditorContext &context)
    {
        auto fields = BuildComponentProperties(context);
        const auto *info = context._component_info;
        if (info == nullptr || !info->_is_enableable || !info->_is_enabled || !info->_set_enabled)
            return fields;
        InspectorField enabled;
        enabled._key = "component_enabled";
        enabled._label = "Component Enabled";
        enabled._read = [info](InspectorScene &scene, ECS::Entity entity) -> std::optional<InspectorValue>
        {
            return info->_has_component(scene.GetRegister(), entity)
                ? std::optional<InspectorValue>(info->_is_enabled(scene.GetRegister(), entity)) : std::nullopt;
        };
        enabled._write = [info](InspectorScene &scene, ECS::Entity entity, const InspectorValue &value)
            { info->_set_enabled(scene.GetRegister(), entity, std::get<bool>(value)); };
        enabled._capture = [info](InspectorScene &scene, ECS::Entity entity) -> std::any
            { return info->_is_enabled(scene.GetRegister(), entity); };
        enabled._restore = [info](InspectorScene &scene, ECS::Entity entity, const std::any &state)
        {
            if (info->_has_component(scene.GetRegister(), entity))
                if (const auto *value = std::any_cast<bool>(&state))
                    info->_set_enabled(scene.GetRegister(), entity, *value);
        };
        fields.insert(fields.begin(), std::move(enabled));
        return fields;
    }
}
