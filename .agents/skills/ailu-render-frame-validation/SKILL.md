---
name: ailu-render-frame-validation
description: Capture and analyze AiluEngine Editor render output for visual or temporal validation, including RenderGraph pass captures and automated SceneView camera sweeps. Use when a task needs repeatable PNG evidence, frame continuity checks, or before/after image comparisons.
metadata:
  short-description: AiluEngine automated render capture and validation
---

# AiluEngine render frame validation

Use this skill when visual evidence must be captured from a running AiluEngine Editor and analyzed reproducibly. Select the smallest capture mode that answers the question:

- **Pass capture:** capture a fixed number of frames from a named RenderGraph pass/resource.
- **Camera sequence:** capture continuously while SceneView moves, then inspect temporal changes, settling, or view-dependent artifacts.

Read [references/workflow.md](references/workflow.md) for selectors, driver examples, continuity checks, analysis, and cleanup.

Before capturing, identify the project, open Editor session, scene, pass/resource, camera, output location, and the exact comparison question. Prefer the available `render.capture_pass` / `render.capture_status` tools; otherwise use the repository client `Tools/render_capture_client.mjs`. Inspect current scripts and automation registrations because names and arguments can change.

Use a unique output directory for each run. Preserve existing dirty files and scene/editor state. If the workflow changes a runtime property or camera, record its original value and restore it in a `finally` path, including after errors. Do not save test-only scene changes. Avoid overlapping capture sessions and bound retries for a busy automation pipe.

Store capture-session files and diagnostic artifacts (captures, manifests, logs, analysis, and comparison images) under a unique session directory beside the actual `Editor.exe` being exercised. Do not put these session artifacts under the engine repository's `Saved/` directory. Resolve the executable path from the running Editor process rather than assuming a build output location; if the process path is unavailable, use the selected Editor executable's directory and record that choice.

Treat these as separate evidence levels: source inspection, Debug build, capture transport completion, image analysis, and live visual acceptance. Report which levels were completed. A completed session proves that frames were written; it does not by itself prove that the selected resource was correct, the frames are contiguous, or the visual issue is fixed.

For temporal comparisons, verify manifest counts and sort by `capture_index`; also check `engine_frame` gaps. Align the same world-space region when camera motion changes perspective. Compare against a stationary baseline and report local metrics (such as ROI RMS and P99), worst frame pairs, and representative images. State the capture load, resolution, frame rate, scene settings, and analysis limitations. A measured result applies to that path and setup only.
