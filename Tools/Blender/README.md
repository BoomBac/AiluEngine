# AiluEngine Blender Live Link

## Requirements

- Blender 3.6 or newer.
- AiluEngine Editor with the `BlenderLiveLink` plugin deployed under `Plugins/BlenderLiveLink`.
- Both applications running on the same Windows machine. The bridge listens only on `127.0.0.1:17777`.

## Install

Run `BuildBlenderLiveLinkAddon.ps1`, then install the resulting `dist/ailu_live_link.zip` in Blender through
**Edit > Preferences > Add-ons > Install from Disk**. Enable **AiluEngine Blender Live Link**.

## Connect and sync

1. Start the AiluEngine Editor and open an editable scene.
2. In Blender, open **View3D > Sidebar > Ailu** and connect to `127.0.0.1:17777`.
3. The add-on sends a full sync after connecting. Use **Full Sync** after changing scenes or when recovering
   from a queue overflow.
4. With auto-reconnect enabled, the add-on reconnects after Editor restarts and performs another full sync.

## Supported data

- Object UUID, name, hierarchy, local transform, and deletion. Local datablocks store their GUID as a custom
  property. Linked-library datablocks cannot store custom properties, so they use a deterministic UUID derived from
  the normalized library file path, datablock type and name, which stays stable across Blender sessions and file saves.
- Current evaluated bone/vertex-parented poses are converted to the ordinary object-parent frame. Affine transforms
  containing shear use deterministic `__AiluAffine_` helper parents, retaining exact placement and shared instance meshes.
  Helpers update incrementally and are removed when no longer required; this is a static/evaluated scene bridge, not an Engine skeleton import.
- Evaluated static mesh triangles, split normals, tangents, UV0, material slots, and bounds.
- Principled BSDF base color, roughness, metallic, emission, and image textures through reroutes and node groups.
- Complex surface graphs use isolated CPU Cycles channel bakes, including nested Mix/Add Shader, group defaults,
  Math/Map Range/Clamp masks, color adjustments/layers, procedural textures, Mapping and other UV projections.
- Complex objects receive a bake UV atlas; the same evaluated atlas is sent with the mesh. UV-only unmodified
  mesh instances share bakes; coordinate-sensitive materials and modifiers retain object-specific resources.
- Normal Map and procedural Bump chains are baked into the transmitted atlas's tangent space.
- Alpha supports both clipping and continuous blending. Culling follows Blender for opaque and transparent materials.
  Transparent helper planes and volume-only helper surfaces stay invisible. Image and nested group edits invalidate
  dependent object materials without requiring Full Sync.
- Roughness/metallic image packing supports separate RGB channels and Image Texture alpha, written as linear data.
- Directional, point, spot, and area lights. Disk area lights use the supported rectangular approximation.
- Perspective and orthographic cameras.

The coordinate basis converts Blender Z-up to Ailu Y-up. Mesh payloads are little-endian and carry a per-object
revision. Live Link entities and their resources are transient and are excluded from Scene Asset saves.

## Unsupported data

Shader lobes are reduced to Standard Lit channel values, not an equivalent renderer: Add Shader uses opacity-weighted
surface channels and additive emission; glass refraction and translucency remain PBR approximations. Light Path selects
the camera branch; view-dependent parallax, Incoming coordinates and other ray-dependent effects become static bakes.
Volume density/scattering, world/HDRI, UDIM, skeletal animation, shape keys, geometry nodes, collections and
Ailu-to-Blender synchronization are not transferred.

AUTO exports supported image branches through shared PBR materials. Color adjustments, layered image branches,
Mapping/object coordinates, non-flat projections and non-default normal-map space/UV/strength use native baking
at 2048px. UV-driven graphs retain their original mesh UVs; spatial graphs and explicit `ailu_export_mode=BAKE`
use unique UV atlases to preserve spatial differences between overlapping islands. Baking can be expensive on
large meshes. `DIRECT` explicitly forces the primary-image approximation path; scene ambient occlusion is left
to engine lighting.

For a primary Principled normal driven by an unmodified `Object -> Image Texture (BOX/Repeat/Linear) -> Bump`
branch, AUTO preserves the original height image and evaluates its BOX projection and bump on the GPU. This
retains repeated detail density instead of fitting every repeat into a surface atlas. Constant strength/distance,
inversion, BOX blend and sRGB/Non-Color input are carried with the material. External coordinate objects,
Mapping or height adjustments, non-default filter width and competing/composed normal branches retain native
normal baking. Additional Blender BSDF lobes remain subject to the Standard Lit approximation described above.
Raster forward, deferred and voxel shading share this path. Compute/DXR currently do not apply normal textures.

If an opaque single Principled closure's base color is exactly the same height image's RGB, raster also samples
the original BOX image for color. A zero-factor RGB Mix is recognized as identity; nonzero adjustments and
layered closures retain baked base color. This preserves cloth color detail as well as its bump. The baked
albedo remains available to existing Compute/DXR consumers.

Bake meshes are limited to four cache entries; channel target images/materials are released between passes.
Full Sync streams each object's materials and geometry consecutively, and source file buffers newly loaded by
export are released without discarding preloaded, generated or dirty user image buffers.

Complex bakes run on Blender's main thread. Material custom properties
`ailu_bake_resolution` (default 2048 for AUTO and BAKE, clamped 16-4096),
`ailu_alpha_mode` (`AUTO`, `CLIP`, `BLEND`, `OPAQUE`) and
`ailu_alpha_cutoff` (default 0.5) control quality and opacity. AUTO treats predominantly fractional alpha as blending;
use CLIP for foliage masks that intentionally contain broad soft transitions.

## Troubleshooting

Transport uses the Blender main-thread timer and nonblocking socket polling. The add-on does not start a background
Python network thread, because Blender operators/Cycles cannot safely run alongside persistent Python threads.
Restart Blender after upgrading from the threaded add-on so the previous module and worker are fully unloaded.

- **Cannot connect:** confirm the Editor is running, plugin autoload succeeded, and port 17777 is free.
- **Protocol mismatch:** rebuild the Editor plugin and install the matching add-on version (protocol v1).
- **Ailu closed the connection:** reconnect and run Full Sync. A queue overflow or invalid payload closes the
  current client so stale updates cannot be applied.
- **Texture missing:** use a supported image texture node and confirm its source file is available. Packed and
  generated images are exported to a temporary session directory.
- **Imports after FullSync:** the summary ends the full batch; actual edits still trigger incremental imports.
  Exporter-generated GUID/bake/scene updates are suppressed and drained so idle export does not resend itself.
- **Material fallback:** check Blender's console for `Baked graph`, `Material approximation` or export errors.
  Ray/view-dependent shading is static, while volume-only helpers are explicitly reported as not transferred.

For the material-export regression smoke test, run Blender with
`--background --factory-startup --python-exit-code 1 --python Tools/Blender/tests/material_sync_smoke.py`.
Run `Tools/Blender/tests/material_bake_smoke.py` with the same flags for native bake channel/UV/cleanup regressions.
Run `Test/BlenderTransformSyncSmoke.py` on a disposable scene copy with `-- <report.json>` for source-to-wire world
matrix, reflected/sheared affine, idle and helper-lifecycle checks. Geometry publication is stubbed in this focused test;
native Editor scene-state and render captures remain separate acceptance levels.

## Validation limits

Material bindings use the evaluated mesh's sparse material table, with OBJECT-linked overrides. Null slots retain
their indices; Geometry Nodes material replacements and converted Curve meshes no longer depend on original object
slot counts. Converted self-instance geometry is published once. Run a new Full Sync after installing this repair.

For generic material acceptance use `Test/BlenderLinkValidation/runner.py --fixture-kind materials` with explicit
`--blender`, `--editor` and `--output` paths. Add `--source-blend <scene.blend>` to validate an isolated copy of a
real scene. The suite checks source/wire/Engine bindings, unexpected checker fallback, controlled GBuffer texture/UV
markers and three bounded incremental edits. See `.agents/skills/ailu-blender-link-validation/references/material-bindings.md`.
Lighting, World environment and tone mapping remain separate from material-slot acceptance.

Transport, plugin lifecycle, Blender UI install, and image appearance need separate acceptance. Use the Editor
transport smoke client for protocol and lifecycle checks; Blender itself is required for add-on interaction and
visual checks.


## Source display settings

Matching Engine/plugin and add-on builds advertise `scene_display_v1`. Full Sync and subsequent exposure/Gamma
changes synchronize Standard or AgX, sRGB, Look None to a Postprocess Volume owned by the imported source root.
Exposure EV becomes `2^EV`; Gamma is applied after the view transform in display sRGB. Source Bloom is not enabled.
Unsupported transforms/displays/Looks produce an Engine warning and disable the source display volume. This is
not a general OCIO or custom Look implementation. A higher-priority authored global volume can override it.

Raster area lighting is no longer limited to four entries. Shadow capacity is separate; excess shadow-casting
area lights still illuminate without a shadow. Point and spot raster arrays hold 32 and 4 lights respectively; skipped excess lights are reported safely.
Point-shadow capacity is a separate budget of four cube maps. Restart Blender after replacing the installed add-on, then Full Sync.


Focused regression entry points:

- `Tools/Blender/tests/test_scene_display.py`: Blender source settings/incremental message and native display reference.
- `Test/ToneMappingSmoke.cpp` + `Test/AnalyzeToneMappingSmoke.py`: production GPU transforms and Gamma/serialization.
- `Test/RasterAreaLightAcceptance.py --output <new-directory-below-Editor> --editor <Editor.exe>
  --project-template <empty.json>` then `Test/AnalyzeRasterAreaLights.py <output>/initial`: independent nine-light
  contribution/additivity, shrink/regrow and delete-all checks. Uses a disposable project and restores shared state.
- `Test/AnalyzeBlenderDisplay.py <native-run> <reference-root> <output>`: source Standard/Gamma against production
  captures. Constant-input GPU checks are separate from observed frame-variation bounds on scene captures.


Use `Test/RasterAreaLightAcceptance.py --light-type point` with the same isolated Editor arguments, then
`Test/AnalyzeRasterPointLights.py <output>/initial` to validate point slots 5/16/32, reject slot 33 safely,
check 32-light additivity and delete-all cleanup. Its half-power diagnostic is separate from capacity acceptance
and verifies point/spot half-power linearity and single application of RGB tint.

`world_environment_v1` sends independent HDR World lighting and camera-background maps. Is Camera Ray branching is
supported; other linked Light Path outputs and World Volume produce an explicit unsupported diagnostic. The current
environment approximation uses 512x256 latlong maps. Source exposure/Gamma stays in the display settings message.
World textures are persisted beside the imported source assets and remain available after Editor restart.

`Test/BlenderWorldSyncSmoke.py` runs in native background Blender to check World split, cache invalidation and cleanup.
`Test/RasterAreaLightAcceptance.py --metallic-probe` plus `Test/AnalyzeMetallicAreaLights.py <output>/initial` checks
metal highlight colors and equal world-area emitters with different authored dimensions and transform scales.
