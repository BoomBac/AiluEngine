# Spaceship ground/background parity and 32-point-light capacity

Purpose: Diagnose ground/background mismatch; expand raster point-light cbuffer to 32.
Scope: Shared layout, shader/fog consumers, separate four-cube point-shadow budget; controlled original-scene captures.
Non-goals: Claim full Eevee equivalence, tune source material/exposure arbitrarily, general World graph compiler.
Architecture constraints: Shared ShaderInterop layout, sizeof-derived buffers, 256-byte alignment and 64KiB limit.
Preserve original blend, user processes, shared config and dirty checkout; use isolated validation lifecycle.
Relevant files: ShaderInterop.h, RenderConstants.h, constants.hlsli, ShadowSystem.cpp, VolumetricFog.cpp;
deferred_lighting.hlsl, material_sync.py, Blender validation/capture tools.
Current milestone: 1, implementation/build then focused capacity and original-scene tests.
Progress: Found duplicated shader macro and three fog paths hardcoding four points; split shadow allocation capacity.
Decision Log: 32 light entries, 4 point-shadow cubes; preserve excess-light warning and guard.
Surprises & Discoveries: Missing fx3_Panels_Bump2.jpg feeds Base Color, not Normal. Old camera captures are mirrored.
Validation: Pending; rendering and texture-sampling architecture plus both validation skills read.
Outcomes: Pending.


Progress update:
- build-debug passed with shared point capacity 32, shader macro sharing that definition, fog count/hash/debug
  consumers updated. Scene CB padding adjusted to preserve 256-byte alignment and <=64KiB compile-time guard.
- Four point-shadow cubes remain independent; source imported light shadows are false even though Blender use_shadow
  is true. This is a confirmed sync gap, not yet a quantified cause of the dark floor (missing shadows brighten).
- Native Eevee World-off control changes the actual wall/floor appearance strongly. Baseline/World-off scene-linear
  frame means ~0.0760/~0.0331. Image EXR values verified against source emission; raw render controls retained.
- Source floor/background Material.002 uses UV connected directly to Principled Normal. Existing exporter bakes
  a normal texture for each Cylinder; not evidence of a missing Normal transfer. Geometric-normal A/B retained.
- Source audit: all lights normalize=true, one area has non-unit scale. Area F0 shader currently hardcodes dielectric
  gray instead of metallic/base-color interpolation. Point GetPointLight multiplies a color already included by
  GetPointLightIrridance; added isolated half-power capture to quantify repeated radiometric multiplication.
- 33-point fixture running with 600s bounded cold start; shader interop layout change invalidates all shader variants.
Evidence root: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/ground_background/20261017-123731.


Milestone 1 result: PASS. 33-point fixture capture/cleanup passed; independent pixel oracle confirms slot 32
contributes, slot 33 is skipped, all32/first4 additivity and deletion-to-zero. Half-power diagnostic is separately
CONFIRMED_DEFECT: ratio 0.25 vs physical 0.5, consistent with radiometric color multiplied twice. This diagnostic
is intentionally not reported as an overall lighting pass. This milestone changes capacity; physical defects are retained as separate diagnosis.
Current milestone: 2, original-scene native controls and numerical/visual report.


Final milestone result:
- Capacity implementation: Debug build PASS, 33 stable GPU frames, slot 32 contributes and slot 33 safely skips;
  original source Full Sync PASS; fifth point contribution is present but zero in the measured floor region.
- Source World-off control: wall/floor scene-linear means decrease ~58%/~30%; imported Environment list is empty.
- Key floor cause confirmed: all 15 source ray samples hit double-sided backs with downward geometric normals.
  Engine GBuffer floor mean normal Y=-0.98248. Cull None is applied, but backface shading normal is unflipped.
- Independent native source-copy control reverses 45 Material floor faces on each of four Cylinder objects.
  Floor albedo remains bit-identical, normal Y becomes +0.9999999, mean HDR rises 0.00679775 -> 0.05477773 (8.058x).
  Control does not change exposure, light power or material values. This is diagnosis, not a source-mesh workaround.
- Half-power isolated point test confirms preexisting repeated-radiometric-color defect: 0.25 vs expected 0.5.
  Area metallic F0 omits albedo/metallic interpolation; static diagnosis, contribution not independently quantified.
- Floor GBuffer roughness/metallic medians .364706/.725490 match source Material .366292/.725843 within 8-bit storage.
- Original, control and point fixture owned processes exited, shared state restored to each run baseline, original
  source SHA256 unchanged. Final production build-debug, targeted diff checks and Python/Node syntax checks PASS.
- Report: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/ground_background/20261017-123731/report.md.
Current milestone: Complete for requested capacity and diagnosis.
Outcomes: 32-light cbuffer delivered. Ground/backdrop causal diagnosis delivered; full Eevee parity is not accepted.
Follow-up repairs remain: shared double-sided shading normal semantics, World lighting/background sync, repeated
point/spot color, metallic LTC F0, Principled IOR/specular and source shadow flags. No production changes to these
remaining rendering semantics were made in this diagnosis stage; original mesh was never rewritten.
