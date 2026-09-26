# Capture and analysis workflow

## 1. Preflight

1. Resolve the actual project root and scene path. Confirm the Editor is open on that project and that `.ailu/editor_session.json` or the Editor automation tools refer to the same instance.
2. Check whether the running Editor contains the intended binary. After a build, compare executable/DLL timestamps and restart the Editor before interpreting the capture.
3. Record the starting camera pose/FOV, runtime properties to be toggled, scene save state, and relevant Editor configuration files. Write outputs to a new `Saved/<run-name>` directory; never reuse a directory with prior sessions unless the analyzer selects a specific session.
4. Define the question before capture: target pass/resource, output versus input, camera ordinal, baseline/control, motion path, expected frames, and a measurable ROI.

For a targeted algorithm change, change one factor per run. Keep scene settings, camera path, warm-up, capture count, and analysis method the same across before/after runs.

## 2. Fixed pass capture

Prefer the `render.capture_pass` tool when available. Supply the exact pass name and, where needed, resource name, pass ordinal, access index, camera ordinal, output/input selection, frame count, and a unique output directory. Poll `render.capture_status` until a terminal state. Record the returned session and file list.

If MCP capture tools are unavailable, use the repository client from the engine checkout:

```powershell
$project = "D:/workspace/AiluTestProj" # replace with this device's project checkout
node Tools/render_capture_client.mjs status --project $project
node Tools/render_capture_client.mjs capture --project $project --pass "DeferredLightingPass" --frames 8 --wait
node Tools/render_capture_client.mjs capture --project $project --pass "RayTrace GI DXR" --resource "RayTraceGI_A" --frames 8 --wait
```

The current client has a machine-specific default project root, so pass `--project <project-root>` explicitly on shared or multi-device workflows. `--input` captures a pass input. `--prefix`, `--access_index`, and `--camera_ordinal` can narrow the target. Use the pass/resource names present in the current graph. For example, `DeferredLightingPass` is a known selector in the referenced Surfel scene; do not assume that selector applies to another scene or build.

If a capture waits without finding a target, verify the exact pass name, resource/access index, camera ordinal, and that the intended pass ran in this Editor session. For black or empty output, check the selected graph resource and whether it was written before investigating readback timing; a completed GPU fence alone does not establish valid pixels.

## 3. Continuous SceneView sequence

Use a single long-running pass capture while driving the SceneView camera. Do not capture one frame per waypoint: pauses alter temporal behavior and hide short-lived changes.

1. Save and log the original camera state and any runtime debug/property values.
2. Move to the designated start pose with a snap, let the scene warm up, then capture a stationary baseline.
3. Start one continuous capture with enough frames for the complete motion plus a settling tail. Drive the camera with smooth position updates (`snap=false`); a cosine path through each reversal avoids abrupt velocity changes. Log requested and returned camera poses with elapsed time.
4. Wait for terminal capture status, verify the returned frame list, and restore the original camera and runtime properties even if a step fails.
5. Repeat controls as separate runs with a fresh Editor/history where the comparison requires independent initialization.

The helper at `Saved/SurfelContinuousCameraValidation/run.mjs` is a concrete example, not a generic runner: its scene entity GUIDs, positions, durations, warm-up, and debug property names are scene-specific. Copy only the orchestration pattern after replacing and checking these values. It records the original camera, warms up, captures baseline and motion, polls status, logs actual camera observations, and restores the camera in `finally`.

Automation named pipes may report busy while another client is connected. Serialize calls and use bounded retry with a short delay; do not launch parallel RPC clients against one Editor pipe or retry indefinitely. Only one capture session should be active at a time.

## 4. Validate and analyze the sequence

- Confirm capture state is `completed`, expected frame count matches the manifest, every file exists, and dimensions are consistent.
- Sort manifest entries by `capture_index`; the order in which files are written or returned may differ. Check consecutive `engine_frame` values and report gaps or duplicates instead of inferring continuity from PNG filenames.
- Compare the moving run with the stationary noise baseline. For moving-camera comparisons, register the same wall/object/world region to remove expected perspective scaling. Preserve the raw sequence as well as registered analysis.
- Inspect local ROI metrics and worst adjacent frame pairs. Useful measures include RMS and P99 absolute channel difference in 8-bit output space; these are image stability measures, not linear radiance or physical error.
- Make an HTML frame viewer, filmstrip, worst-pair sheet, or animation when a long sequence needs review. Keep the raw PNGs, capture manifests, camera observations, runtime log, analysis output, and a concise report together.
- Check capture-induced frame rate and motion-control update rate. For example, the referenced run captured around 10–11 fps and drove the controller at roughly 6 Hz; conclusions are limited to that loaded capture setup and do not establish normal-frame-rate or mouse-input behavior.

## 5. Report and restore

Report the scene and settings, selector, output dimensions, requested and actual frame counts, engine-frame continuity, warm-up and trajectory, capture rate, ROI/registration method, baseline, summary and worst-case metrics, and links to representative artifacts. State whether the result passed, failed, or remains inconclusive, and name any untested path.

Keep the evidence categories explicit:

- **Build:** which Debug preset/target succeeded, if a build was needed.
- **Capture:** which pass/resource was captured and whether transport completed.
- **Sequence integrity:** frame count and engine-frame gaps.
- **Visual analysis:** measured comparison and limitations.
- **Live acceptance:** whether the intended Editor view/path was actually reviewed.

On cleanup, restore runtime-only changes and camera state; do not save test scene changes. Restore Editor config files only when the run changed them and only against the exact pre-run backup, preserving edits made by others during the run. Check hashes/status before overwriting. Close only Editor/test processes started for this run. Keep the unique evidence directory and note any incomplete restoration.
