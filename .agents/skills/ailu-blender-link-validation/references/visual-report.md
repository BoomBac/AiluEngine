# Material visual report evidence

Material validation reports require three embedded PNGs that show the same source scene and corresponding view:

1. Blender background Eevee render of the staged source scene.
2. Engine GBuffer base color (`_GBuffer1` from `DeferedGeometryPass`).
3. Engine final output (`_CameraColorAttachmentA` from `DeferredLightingPass`).

The Markdown report generator refuses to create the report if any image is missing. Keep the original Blender file
unchanged. Render the runner's disposable source copy, and use the runner's existing GPU captures for both Engine
images.

## Blender Eevee background render

After the `materials` runner finishes, read `summary.json` and use its `source_blend` path (the isolated copy, not
`original_source`). The copy is staged as `fixture/materials_fixture.blend`.

From the repository root, run the helper with the same Blender executable used by the runner:

```powershell
& <blender.exe> --background --factory-startup <runner-stage-directory>/fixture/materials_fixture.blend `
  --python Test/BlenderLinkValidation/render_blender_preview.py -- `
  --output <runner-stage-directory>/blender_eevee_preview.png
```

The helper requires an active scene camera, uses Eevee, renders at 1280x720, and writes PNG without saving changes to
the staged `.blend`. The scene's camera, lights, and World are used. This is a Blender Eevee background render, not a
pixel-equivalent reproduction of viewport Material Preview's studio HDRI lighting; label it accordingly.

## Engine images

For the standard material suite, `capture_material_bindings.mjs` already captures three consecutive frames after
setting the camera from the independent material reference. It restores the original Editor camera in `finally`:

- Base color: pass `DeferedGeometryPass`, resource `_GBuffer1`, access index `1`, PNG output, camera ordinal `0`.
- Final output: pass `DeferredLightingPass`, resource `_CameraColorAttachmentA`, camera ordinal `0`; native DDS is
  retained and the report helper creates a Reinhard plus sRGB PNG preview for embedding.

The driver is launched by `runner.py --fixture-kind materials`. Do not start a second capture client against the same
Editor session. Verify `material_capture.json`, `material_image_assertions.json`, and each `capture.json`. Require
completed sessions, three frames, existing image files, ordered `capture_index`, and consecutive `engine_frame` values.
Choose the last valid frame from each capture for the report. These images document output; they do not assert exact
RGB parity with Blender because lighting and color transforms differ.

If driving capture manually because the runner did not capture, use the installed `render.capture_pass` and
`render.capture_status` tools from the render-frame-validation skill, or its
`Tools/render_capture_client.mjs` fallback. Use the exact selectors above, current project path, and three frames.
Run one session at a time and verify the manifest before embedding anything. Do not guess pass names from another
scene/build.

## Generate the report

After both Engine captures and the Blender Eevee render exist, run from the repository root:

```powershell
python Test/BlenderLinkValidation/make_skill_report.py `
  --stage <runner-stage-directory> `
  --scene <scene-name> `
  --source-blend <original-blend-path> `
  --blender-render <runner-stage-directory>/blender_eevee_preview.png
```

The report is written beside the stage as `skill_report_<scene>_<date>`. Its Markdown includes the three images using
relative embedded-image links. The copied report assets are `blender_eevee_preview.png`,
`render_captures/material_albedo/`, and `render_captures/material_final/`. Keep the original Engine captures and
manifests in the stage for auditability. If Full Sync or either GPU capture failed, report that outcome and the
missing evidence; do not substitute a screenshot from a different scene or call the three-image requirement satisfied.
