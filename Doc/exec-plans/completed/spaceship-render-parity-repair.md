# Spaceship rendering parity repair

Purpose: Implement the four approved repairs established by ground/background diagnosis.
Scope: Shared double-sided raster face normal policy; point/spot radiometric multiplication; metallic LTC F0;
source World lighting/background separation and incremental/persistent Link sync; focused GPU/native regressions.
Non-goals: Full general OCIO, arbitrary ray-type-dependent World closure equivalence, change original mesh/material values.
Architecture constraints: Existing surface helpers and EnvironmentSystem own render semantics; plugin owns root component
and durable texture assets; no Editor dependency in Engine. Preserve current authored environment behavior by default.
Relevant files: standard_lit_common/defered_standard_lit/forwardlit/lighting; Environment/Component/RenderingData;
Blender scene_sync, new world exporter, protocol/session, existing native validation lifecycle.
Current milestone: Complete; approved implementation and regression acceptance finished.
Progress:
- Shared deferred/forward face orientation, point/spot single-color application and metallic LTC F0 implemented.
- Principled IOR/Specular IOR Level reaches shared dielectric F0, stored in deferred GBuffer3 alpha.
- Rectangle radiance uses transformed world quad area in the shared raster/RT extraction helper.
- WorldUpsert 26 exports independent lighting/background EXRs, imports durable source assets and root environment.
- Both texture GUIDs serialize/load through scenes and prefabs and participate in dependency tracking.
- Installed add-on backed up and refreshed; source/install Python hashes match; zip rebuilt.
Decision Log:
- Shader fixes retain existing lighting units. World camera/lighting paths remain distinct from display transform.
- Empty background GUID preserves old authored cubemap skies. World uses HDR import settings, linear and uncompressed.
- World exporter uses temporary Raw CPU scenes; 512x256 approximation, Is Camera Ray supported, other ray splits and
  World Volume explicitly unsupported. No original blend/global preference writes.
Surprises & Discoveries:
- Corrected earlier audit: the actual source Environment Texture has `00-1.jpg` assigned. A bpy Image truthiness
  check incorrectly classified it as missing; `is not None` is required.
- First restart exposed missing EXR support in TextureImporter despite the external HDRParser supporting it.
  Added TinyEXR decode to the artifact importer and HDR import settings; initial failed restart evidence retained.
- First final-image inspection exposed old TextureSky default clear behavior. Preserve existing color/depth and
  use far-plane equality without depth writes. Deferred-light captures were valid but early final-display captures
  were invalid as acceptance evidence. Final restart reruns the corrected path.
- First area scaling fixture used an obsolete `_area` field; corrected current `_light._area_width/height` fields.
- EXR reload reached an existing rectangular mip-size bug: the short dimension became zero at the last level.
  Fixed the shared 2D size helper to clamp both dimensions to one. Subsequent GPU upload succeeded.
- Additional restart audit showed ground GBuffer empty (normal Y zero, albedo zero), not wrong light restoration.
  Light matrices/power matched within serialization precision. `_cull` was saved but SetInt restored only its map,
  leaving the raster cull mode at shader default. Restore `_cull` through SetCullMode in the existing setter.
- Surface/keyword rebuild also reset the cull map before material save. Preserve its value in Construct(false),
  retain shader defaults for first construction, and regenerate source assets with a fresh Full Sync. Previously
  exported incorrect cull values cannot be inferred from the asset alone; the original source remains authoritative.
Validation:
- `cmake --build --preset build-debug`: PASS, final log Temp/parity_repair/build_final_verified.log.
- `git diff --check`: PASS (line-ending notices only).
- Native Blender World split/incremental/cache/cleanup smoke: PASS; analytical direction error below 0.004.
- Point/spot half-power ratios: 0.5 / 0.49999994; per-channel color once, 32/33 capacity/additivity: PASS.
- White-area metal RGB probes: dominant channel over 200 times other channels, PASS.
- Equal world emitters represented as width/height 1 at scale 1 and width/height 0.5 at scale (2,1,2):
  bit-identical actual GPU lighting, PASS. Corrected fixture evidence: metallic_scaled_final/initial/metal_analysis.json.
- Native spaceship Full Sync: one Begin/End, original source SHA256 unchanged, owned processes exit zero,
  shared-state restoration PASS. Final source run: parity_repair_native/20261017-170754-f8bfe1bf beside Editor.
- First original-scene floor normal Y +0.9824875 versus previous -0.98248; floor dielectric F0 0.02357483
  versus source 0.02357724. Final restart ground normal Y +0.9824935 and F0 0.02354431, PASS.
- Final reloaded floor/wall HDR means differ from live sync by 1.36% / 0.25%; not pixel-identical. World IBL on/off
  contributes measurable floor/wall illumination. Final restart_accepted restores shared state and exits owned process.
- Actual final ToneMapping and HDR previews visually inspected; cyan floor and textured background restored.
  Independent Eevee source preview and three required images embedded in the report. Full Eevee parity remains unclaimed.
- Source compositor/sequencer made the first helper preview black. The background preview helper now explicitly
  disables these stages; rerendered isolated source was visually checked and used in the final three-image report.
Evidence: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/parity_repair/20261017-133416.
Outcomes: Four approved repairs complete; native and GPU regression PASS. report.md and acceptance.json contain final
evidence. Historical failed captures retained and excluded from final acceptance. Original blend, global preferences,
shared config and unrelated dirty changes preserved. Installed add-on and packaged zip updated; existing wrong cull
assets require a fresh Full Sync. Known Type::Find, exit leaks, sampler/DSV warnings and full Eevee/OCIO parity remain.
