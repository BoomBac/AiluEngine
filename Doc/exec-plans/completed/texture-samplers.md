# Texture sampler refactor

## Purpose
Support texture defaults, material-slot overrides and per-view mip bias with DLSS/FSR.

## Scope / Non-goals
Keep fixed samplers for utility passes. No streaming, virtual textures or ray-cone LOD implementation.

## Architecture constraints
Descriptor manager owns an immutable deduplicated sampler heap; descriptors remain valid until GPU-idle shutdown.
Texture owns authoring defaults, Material owns overrides, Renderer owns manual bias, Camera resolves view compensation.
Preserve unrelated dirty work, including command-buffer state caching and material variant fixes.

## Relevant files
Texture.h, Material.*, ShaderInterop.h, Renderer.*, Camera.h, DX12 descriptor/root-signature/binding code, asset documents/handlers.

## Current milestone
Completed: implementation, Debug build and focused GPU validation.

## Progress
- [x] M1 Engine Debug compile/link, sampler cache identity/normalization/stability and JSON roundtrip passed.
- [x] M2 texture defaults/slot persistence and shader integration; six GPU color checks and clean shutdown passed.
- [x] M3 view bias, controls, Debug build and focused GPU/raster validation.

## Decision Log
- No TEMPLATE.md exists at the instructed path; this plan includes the required sections directly.
- Append-only sampler cache is bounded by the DX12 sampler heap limit (2048); fail explicitly on exhaustion rather than overwrite in-flight entries.
- Global/view bias stays in shader constants, outside sampler-cache keys.

## Surprises & Discoveries
Existing FilterMode/WrapMode do not drive standard material sampling. Reflection identifies samplers but currently skips dynamic bindings.

## Validation
Final full `cmake --build --preset build-debug` passed (exit 0). Touched-file `git diff --check` passed.
DXC checks passed for GBuffer/Forward/DepthOnly/MotionVector/ShadowCaster, Compute ray generation, DXR library, DDGI and Surfel paths.
Final runtime evidence: `F:/AiluEngine/artifacts/out/test/bin/x64/debug/SamplerSmoke_20261015_143320`.
`checks.txt` PASS, exit code 0, clean shutdown, six exact expected Compute samples, no D3D12 validation exception.
`Test/AnalyzeSamplerSmoke.py` passed all five 4x4 float DDS captures: base, automatic, opt-out, combined and negative bias; all expected pixels matched. PNG transport also completed and representative previews were inspected.
Actual Editor interaction, DLSS/FSR temporal quality and runtime DXR dispatch remain unverified; these focused results do not claim that acceptance.

## Outcomes
Texture defaults, material slot overrides, immutable shared native samplers and manual/per-camera SR bias are implemented.
SceneView Camera controls expose the bias policy. Asset compatibility and focused GPU behavior passed.
Long-term design and first-version limits are documented in `Doc/architecture/texture-sampling.md`.

M1 validation: Engine target built through build-debug. SamplerSmoke checks.txt PASS in F:/AiluEngine/artifacts/out/test/bin/x64/debug/SamplerSmoke_20261015_110308.
Initial loader failure (0xC0000135): FBX/PIX DLL deployment added to test target. A no-frame test teardown left queued commands until after CommandPool shutdown; test now presents/drains before Finalize. Full shutdown and sampler GPU sampling remain final-stage checks.

M2 validation: F:/AiluEngine/artifacts/out/test/bin/x64/debug/SamplerSmoke_20261015_112208 contains all six expected GPU colors, checks.txt PASS and shutdown.txt.
DXC GBuffer, Compute RayGen and DXR library passed. Final 128-byte transient MaterialData layout will be rebuilt in M3.
Discovery: default Compute 2D SRV exposed only mip0; default bindings now expose the full mip chain, explicit subresources stay single-mip.
Discovery: native sampler MipLODBias applies to SampleLevel too (confirmed by GPU and Direct3D functional spec). Removed duplicate shader addition of texture bias.

M3 harness discoveries: scene activation requires a registered pipeline; raster material allocation requires normal frame initialization. The final test runs an empty scene through the normal pipeline/frame lifecycle. DirectXTex writes legacy A32B32G32R32F DDS headers for this format; the analyzer accepts both legacy and DX10 float headers.
