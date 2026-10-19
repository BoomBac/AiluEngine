# Physical Lighting System Refactor

## Purpose

Define a coherent light data contract and align scene serialization, Inspector controls, Blender Live Link,
raster shading, and ray tracing around it. Remove range-dependent brightness and preserve existing scene data
through an explicit migration.

## Scope / Non-goals

- In scope: directional, point, spot, and area lights; intensity/color separation; range, source size, spot cone,
  area shape/two-sidedness, serialization compatibility, Inspector, Blender Live Link, raster and RT.
- Out of scope: emissive materials, environment lighting, exposure/tonemapping redesign, shadow architecture,
  volumetric-only controls, unrelated BRDF changes.

## Architecture constraints

- Keep authored component data in ECS and derive renderer-facing packed data at scene extraction.
- Store authored intensity unit and convert once to canonical renderer radiance.
- Range affects support, not source power.
- Keep legacy scene/prefab fields loadable and migrate deterministically.
- Do not add a global light manager or make Engine depend on Editor/plugin code.
- Update generated reflection through the normal build/codegen path.

## Relevant files

- `Engine/Inc/Scene/Component.h`
- `Engine/Inc/Assets/AssetDocument.h`
- `Engine/Src/Scene/EntitySerializer.cpp`
- `Engine/Src/Scene/PrefabSystem.cpp`
- `Engine/Src/Render/Renderer.cpp`
- `Engine/Src/Render/RayTracing/SceneRayTracingProxy.cpp`
- `Engine/Res/Shaders/hlsl/punctual_lighting.hlsli`
- `Engine/Res/Shaders/hlsl/lighting.hlsli`
- `Engine/Res/Shaders/hlsl/ray_trace/path_tracer_light_common.hlsli`
- `Editor/Src/Inspector/ComponentInspectorFields.cpp`
- `Editor/Src/Inspector/ComponentEditors/LightComponentEditor.cpp`
- `Editor/Src/Automation/AutomationReadModel.cpp`
- `Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp`
- `Tools/Blender/ailu_live_link/light_sync.py`
- Related scene/prefab cloning, shadow, gizmo, light-sampling, and validation code.

## Current milestone

M4 - Complete focused runtime validation and record limits that still require a visual acceptance scene.

## Progress

- [x] Read rendering, scene/ECS, reflection, and serialization architecture guidance.
- [x] Trace ECS, Inspector, automation, Blender Live Link, raster, RT, shadows, and principal serialization paths.
- [x] Confirm unit convention with user: Unreal-style photometric controls plus explicit Blender radiometric units.
- [x] Implement named authored light fields and deterministic scene/prefab legacy migration.
- [x] Align Inspector, automation read model, gizmos, and Blender power/range transport.
- [x] Convert photometric and Blender radiometric units at renderer extraction.
- [x] Add optional Kelvin blackbody tint through ECS, serialization, Inspector, automation, Blender Live Link, and raster/RT extraction.
- [x] Remove range-dependent point/spot brightness in raster and RT; apply range as support window.
- [x] Apply the same support window to analytic area lights in raster and RT.
- [x] Run `cmake --build --preset build-debug` successfully after code generation.
- [x] Update stable rendering architecture documentation.
- [ ] Capture and inspect an actual Editor scene using the new unit controls.

## Decision Log

- User selected an Unreal-style photometric workflow: local lights use candela/lumen, directional lights use lux,
  old unitless data has a migration path, and Blender radiometric W / W/m2 remains explicit.
- Store the selected authored unit and convert once to canonical renderer radiance. Punctual attenuation uses
  inverse-square falloff; range is an independent smooth cutoff.
- Blender RGB color has no spectral power distribution. Any W-to-photometric conversion must state a fixed RGB
  luminous-efficacy approximation; preserve native W units when importing to avoid pretending it is exact.
- Existing ECS `LightData` stores RGB plus intensity in color alpha and packs type-specific parameters in
  `_light_param`; renderer paths apply additional scaling independently.
- Current punctual attenuation includes `range^2 / (distance^2 + 1)` before a cutoff window in both raster and RT,
  so changing range changes both brightness and support.
- Raster uploads multiply every light type by pi. Path tracing separately converts premultiplied color into
  directional solid-angle radiance, point/spot inverse-square radiance, and area radiance.
- Light data is duplicated through scene documents, scene load, prefab instantiate/apply/revert, component cloning,
  Inspector/read-model fields, raster packing, shadow camera setup, debug gizmos, and ray-tracing extraction.
- Scene documents have a versioned migration path. Prefab documents do not, so light data needs an explicit schema
  marker to migrate old prefab fields without a whole-prefab version transition.
- `Doc/exec-plans/TEMPLATE.md` is absent in this checkout despite repository guidance referencing it.
- Git status could not be read because Git reported permission denied for loose objects and user config. Changes will
  be scoped to reviewed hunks and existing source content will be re-read before edits.

## Surprises & Discoveries

- Raster area lighting consumes source geometry and color but ignores its packed range parameter.
- Blender area `shape` currently crosses the Python boundary but the engine's area-light renderer is rectangle-only.
- Blender Live Link exports `energy` and `cutoff_distance` without checking `use_custom_distance`.
- `scene.unit_settings.scale_length` is not applied in the Blender exporter.
- The existing scene format version supports migrations, but per-light schema marker is safer because the same
  entity document is also embedded in prefabs.

## Validation

- M0 audit: source-path and formula review completed.
- Schema milestone: scene/prefab old-data migration, component clone/revert, and generated-code review.
- Rendering milestone: `cmake --build --preset build-debug` passed after the final C++ changes. The build does not
  compile the edited runtime HLSL paths. No Editor process or connected capture session was available, so matched
  HDR captures and visual acceptance remain pending.

## Outcomes

The source contract now stores intensity, unit, range, source size, cone, area parameters, and optional Kelvin
color temperature separately. Unit and blackbody color conversion is centralized in `Scene/LightUnits.h`; RGB
color is treated as linear and photometric intensity is converted with a fixed
683 lm/W efficacy for photometric inputs because the engine has no spectral power distribution. Blender W and W/m2
remain radiometric inputs. Legacy unitless scene/prefab lights retain a marked compatibility scale.

The current renderer still represents analytic area lights as rectangles. Disc data is serialized and shown in the
Inspector, but raster/RT geometry does not yet emit a true disc. Blender `scale_length` also remains unapplied, so
scene coordinates are interpreted in engine length units. These are the next follow-up items before claiming full
physical agreement across Blender and Ailu.
