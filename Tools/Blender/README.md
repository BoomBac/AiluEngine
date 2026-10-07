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

- Object UUID, name, hierarchy, local transform, and deletion.
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

AUTO exports supported primary image branches through shared PBR materials and retains original mesh UVs.
Color adjustments, layered image branches and Mapping/object coordinates use the explicitly reported direct-path
approximations; scene ambient occlusion is left to engine lighting. Unresolved procedural/surface branches fall
back to native baking at 256px. Existing UVs are reused, including their overlap, so spatial variations in overlapping
islands may be approximated. Set material `ailu_export_mode=BAKE` for native graph evaluation with unique UV atlases
at 1024px; this is expensive on large meshes. `DIRECT` forces the existing primary-image approximation path.
Bake meshes are limited to four cache entries; channel target images/materials are released between passes.
Full Sync streams each object's materials and geometry consecutively, and source file buffers newly loaded by
export are released without discarding preloaded, generated or dirty user image buffers.

Complex bakes run on Blender's main thread. Material custom properties
`ailu_bake_resolution` (AUTO default 256, BAKE default 1024, clamped 16-4096),
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

## Validation limits

Transport, plugin lifecycle, Blender UI install, and image appearance need separate acceptance. Use the Editor
transport smoke client for protocol and lifecycle checks; Blender itself is required for add-on interaction and
visual checks.
