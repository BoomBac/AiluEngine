# Blender Link display settings and raster area-light repair

Purpose: Fix the two causes established by race-spaceship-visual-diagnosis: source display settings are absent and raster area lights overflow four fixed entries.
Scope: Standard/sRGB exposure/Gamma synchronization with owned root Postprocess Volume; dynamic frame/view-owned raster area-light buffer; regression and native spaceship validation.
Non-goals: Full Blender OCIO/Looks, world graph parity, new HDR output transforms, unrelated BRDF repairs.
Architecture constraints: Engine owns settings/resources, plugin updates only its source root; linear HDR preserved through lighting/Bloom; output remains display-linear; dynamic area data retained in frame resources and explicitly declared to RenderGraph. Preserve dirty checkout and original blend.
Milestones:
1. Source display message, incremental sync, Standard/Gamma postprocess and serialization defaults; validate against Blender.
2. Dynamic raster area-light buffer, bounds for remaining fixed analytic lists, shadow index and per-view lifetime; focused >4-light tests.
3. Build-debug, native original seven-area-light spaceship Full Sync, pass captures, provenance/restoration and report.
Current milestone: Complete. Final shader/runtime/pixel gates passed.
Progress: Read architecture/render-graph and actual PrepareLight/Shadow/FrameResource/LiveLink ownership. No raster dynamic area-light buffer currently exists; RT unified buffers belong to a different sampling/layout path, so do not couple raster LTC to RT scene extraction.
Decision Log: Use existing root Postprocess Volume and frame resources. Retain fixed scene-cbuffer ABI prefix while migrating raster area consumption to a dynamic SRV; fixed legacy area slots remain reserved and unused by raster. Do not increase the four-light constant.
Validation:
- Production build-debug and standalone LiveLink plugin build passed; source addon packaged and installed with
  persistent previous-install backup. Blender global preferences untouched.
- Blender original-source reference: Standard, sRGB, Look None, exposure EV 0.5029010773, Gamma 0.4100580513.
  Incremental source test sends only one settings message for exposure/Gamma changes; idle sends none.
  Independent encoded formula vs Blender image.save_render max error 0.00168208 (below one 8-bit code value).
- First native spaceship run 20261017-003151-d7dbe9ec: FullSync applied, 55 objects, 12 lights, root Standard
  source volume persisted, original file hash unchanged, owned processes exited and shared settings restored.
  Cyan floor illumination is present with the original seven area lights. This run precedes the final sRGB fix.
- Nine-light controlled captures establish contribution from each light including indices 4-8, additivity of
  all nine, four-light shrink, and zero residual after deleting all. Initial harness incorrectly assumed delete
  supported undo; replaced with real entity creation. Recreated lights initially used default range 10 instead
  of fixture range 0; unified parameters rather than widening acceptance. Final range-matched rerun passed (area_regression4).
- Whole-image same-HDR comparison exposed DXC select arguments reversed in shared ApplySRGBCurve and
  RemoveSRGBCurve. Corrected to select(condition, true_value, false_value); final GPU/runtime checks passed.
- A rebuild while an owned smoke held Streamline DLLs failed during copy only; stopped that owned process,
  then production Debug and updated ToneMappingSmoke builds passed. No user process stopped.
- Evidence root: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_display_area_repair/20261017-003007.

Surprises & Discoveries: Original source also has five point lights. Point/spot raster capacity remains four;
excess now warns and skips safely. Area lighting is dynamic; the separate spot/area shadow slice budget remains
8, and lights beyond that shadow budget illuminate without shadows. AgX remains Minimal approximation, not OCIO.
Final Validation:
- Final native spaceship run: blender_display_area_native/20261017-121228-80faae7a PASS, original source unchanged,
  FullSync Begin/End applied, source settings volume persisted, all shared state restored and owned children exited.
- 40 GPU captures PASS: four modes, exposures 0/.5/1/2, Standard Gamma 1/.410058, FP32 and R11G11B10.
  FP32 maximum absolute error 6.04893021e-6 vs independent double reference; production storage within one ULP.
  Standard/Gamma settings and prefab enum serialization exercised by smoke. None still executes the Pass.
- Nine lights: 42 capture frames PASS. Every independent light including indices 4-8 contributes; all nine add
  within propagated native-format ULP bounds; shrink to four, real recreate to nine and delete-all pass.
- Same Engine HDR through Blender original settings: maximum 2/255 encoded error; p99 <=1/255. Scene input/output
  are captured on separate frames; comparison propagates observed input variation, while constant-input GPU
  captures strictly validate the operator. 10268 cyan floor pixels measured in raw HDR before source Gamma.
- Standalone cold initialization exceeded the old 120s gate; owned run restored, retry deadline 600s. Final smoke
  completed normally after about 3.5 minutes initialization. No timeout classified as a product rendering failure.
- Addon package and installed 16 Python files match source, previous install backed up; no Blender preferences saved.
- Build-debug PASS; targeted smoke build PASS; targeted diff --check and Python/Node syntax checks PASS.
- Aggregate report: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_display_area_repair/20261017-003007/report.md.
Outcomes: The two selected causes are repaired and validated. Full Eevee parity is not claimed: point capacity,
missing external bump texture, World/BRDF and camera alignment remain outside this task. Original source and dirty
checkout preserved. The earlier diagnosis-only run's lost baseline remains documented there; these isolated runs
use persistent backups and verified restoration.
