# Race spaceship Blender Link visual diagnosis

Purpose: Identify the causes of Eevee versus current Engine appearance differences using the supplied race_spaceship.blend.
Scope: Read-only source audit, isolated Full Sync, aligned GBuffer/final captures and focused parameter comparisons. No rendering fixes yet.
Architecture constraints: Preserve original blend, existing Blender PID 43760 and unrelated dirty changes. Reuse validation lifecycle and capture clients. Store evidence beside selected Editor.exe.
Relevant files: Test/BlenderLinkValidation; Tools/Blender/ailu_live_link; Plugins/BlenderLiveLink; Engine rendering shaders.
Current milestone: Source audit and safe reproduction.
Progress: Read validation skills and architecture index/rendering/texture sampling. Existing Blender found; no Editor process found.
Decision Log: Run separate factory-startup background Blender. Compare source material graph and actual export before attributing differences to the renderer.
Surprises & Discoveries: Existing validation runner rejects all preexisting Blender processes even when using a unique port; must preserve existing process and isolate any run.
Validation: Pending source audit, native sync, captures and restoration.
Outcomes: Pending.

Progress update: Two native runs completed. Second run PASS for bindings/capture/restoration relative to its own baseline; roughness/metallic/final each have three contiguous GPU frames. Source original unchanged. Authored-vs-neutral Eevee A/B and identical Engine HDR processed through Blender confirm color-management contribution. Saved evidence and report beside Editor in race_spaceship_diagnosis/report.md.
Decision: No engine fixes authorized by diagnosis request. Corrected horizontal-to-vertical FOV only in temporary diagnostic capture driver. Keep original mirrored captures; comparison sheet explicitly mirrors for display.
Surprise: Eevee compositing/sequencer enabled produced black renders; disabling both produced the reference appearance. First run shared-state restoration skipped because concurrent owned Eevee process was classified external; original in-memory snapshots were lost at runner exit. Do not claim original settings restored or overwrite preexisting dirty files. Second baseline backups persist on disk and restored successfully.
Validation: 616 material-binding checks PASS; native FullSync applies End; GPU roughness/metallic/frame integrity PASS. Exact RGB, light parity, final-screen rendering and cleanup to task-start baseline remain unaccepted.
Current milestone: Remaining lighting/camera parity diagnosis; see evidence report for focused next checks.
Outcomes: Display gamma/exposure contribution established; no global roughness/metallic fallback found; World and Specular IOR Level synchronization gaps identified. Full diagnosis remains open.

Final diagnosis update: Seven source/wire area lights exceed CPU/GPU raster capacity four. Renderer::PrepareLight has unchecked array writes; shader clamps traversal to four. Cyan lights are last in wire order. Third native control removed only Area.003/Area.004/Area.007 in staged copy; cyan contribution returned. Third run 20261016-232838-f695b0ef passed native/capture/restoration to its own baseline. Evidence: race_spaceship_diagnosis/area_capacity.json, area_capacity_ab.json/png and report.md.
Outcome: Main actionable causes identified: display transform mismatch and raster area-light capacity/unchecked upload; scalar defaults ruled out as a global cause. Full visual parity and repairs are outside this diagnosis. No implementation code changed. First-run task-start cleanup remains unaccepted as documented; second/third-run restoration cannot erase that limitation.
Current milestone: Diagnosis report delivered; implementation and full visual acceptance remain future work.
