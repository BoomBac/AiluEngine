# Evaluated mesh and material validation

Use this case for changes to geometry evaluation, material tables, bake UVs or material binding. This is a generic regression, independent of the splash asset.

## Execute
Resolve actual Blender and Debug Editor paths. Run serially in the Windows graphical session, with output beside that Editor:

```powershell
python Test/BlenderLinkValidation/runner.py --blender <blender.exe> --editor <Editor.exe> --output <Editor-directory>/blender_material_validation --fixture-kind materials --capture-debug-output --startup-timeout 600 --timeout 600
```

Add `--source-blend <original.blend> --timeout 1800` for a real scene. The runner creates an isolated copy and checks the original SHA256 afterwards. `--capture-debug-output` captures only the owned Editor PID via Windows DBWIN when file Info logs are filtered; an existing DBWIN listener is a conflict. Set `AILU_BLENDER_PERF_INSTALLED=1` for the runner process to exercise an installed deployment, and verify `export.addon_source` and hashes. Full Sync uses production export functions; registration needs a separate fresh-process check.

## Independent assertions
`make_material_fixture.py` derives expectations directly from `evaluated.to_mesh()`: triangulated indices, sparse mesh material table and explicit OBJECT-linked overrides. It does not call exporter material helpers. Original/evaluated object slots alone are insufficient: a Curve/GN object can expose one object slot while its mesh is `[None, snow]` with faces using slot 1. Preserve empty positions and resolve evaluated materials to original datablocks for stable identity.

Linked-library datablocks cannot store custom properties, so their expectations mirror the production deterministic rule: `uuid5(NAMESPACE_URL, "lib:" + normcase(normpath(abspath(library.filepath))) + ":" + bl_rna.identifier + ":" + name)`; local datablocks keep their stored per-datablock GUID. Library identity must stay equal across independent Blender processes on the same file; verify with a two-process probe when identity changes are in scope.

Converted Curve components can appear as self-instances. Count the actual mesh once, without demanding duplicate original geometry. External prototypes use their own evaluated geometry/material table.

`validate_material_bindings.py` checks source-to-wire table length, empty positions, used indices, material identity, mesh counts and texture existence. Engine compact submesh bindings must match their corresponding source slots and must not have `is_fallback`. Use `scene.inspect_entities` with `include_mesh_summary:true` to avoid huge arrays. A used slot whose source material is empty has no source identity to compare; its empty wire position is asserted by the same empty-preservation check and does not make the suite INCONCLUSIVE.

Negative checks detect removed tables, compressed sparse slots, wrong material identity and injected Engine checker fallback. Controlled GBuffer markers separately detect texture/UV color swaps. Nonblack pixels alone do not establish correct bindings.

## Coverage and interpretation
The synthetic fixture covers ordinary two-slot faces, sparse nulls, OBJECT override, GN Set Material, generated Curve mesh, bevel Curve, external instance, sparse explicit bake and quadrant texture. Sample texture interiors to avoid filtering ambiguity. The native mutation phase checks ordinary rebind, sparse slot/index change and GN material replacement through production `sync_object`, polls actual Engine state and captures changed albedo. This does not exercise the complete dirty timer, reconnect or persistence workflows.

Inspect `material_assertions.json`, `incremental/material_assertions.json`, `material_image_assertions.json`, `material_capture.json`, `run.json` and `restoration.json`. Albedo and final captures require three consecutive Engine frames. Controlled markers assert channel identity, not Blender final RGB equality. Real scenes without markers provide binding/capture evidence plus visual inspection, not automatic pixel parity. Lighting, World and tone mapping require separate diagnosis.
