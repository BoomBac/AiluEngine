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
M3: view bias, controls, final build and validation.

## Progress
- [x] M1 Engine Debug compile/link, sampler cache identity/normalization/stability and JSON roundtrip passed.
- [x] M2 texture defaults/slot persistence and shader integration; six GPU color checks and clean shutdown passed.
- [ ] M3 view bias, Debug build and GPU validation.

## Decision Log
- No TEMPLATE.md exists at the instructed path; this plan includes the required sections directly.
- Append-only sampler cache is bounded by the DX12 sampler heap limit (2048); fail explicitly on exhaustion rather than overwrite in-flight entries.
- Global/view bias stays in shader constants, outside sampler-cache keys.

## Surprises & Discoveries
Existing FilterMode/WrapMode do not drive standard material sampling. Reflection identifies samplers but currently skips dynamic bindings.

## Validation
Pending. Build only with build-debug. Source/build/transport/image/live acceptance remain separate evidence levels.

## Outcomes
In progress.

M1 validation: Engine target built through build-debug. SamplerSmoke checks.txt PASS in F:/AiluEngine/artifacts/out/test/bin/x64/debug/SamplerSmoke_20261015_110308.
Initial loader failure (0xC0000135): FBX/PIX DLL deployment added to test target. A no-frame test teardown left queued commands until after CommandPool shutdown; test now presents/drains before Finalize. Full shutdown and sampler GPU sampling remain final-stage checks.

M2 validation: F:/AiluEngine/artifacts/out/test/bin/x64/debug/SamplerSmoke_20261015_112208 contains all six expected GPU colors, checks.txt PASS and shutdown.txt.
DXC GBuffer, Compute RayGen and DXR library passed. Final 128-byte transient MaterialData layout will be rebuilt in M3.
Discovery: default Compute 2D SRV exposed only mip0; default bindings now expose the full mip chain, explicit subresources stay single-mip.
Discovery: native sampler MipLODBias applies to SampleLevel too (confirmed by GPU and Direct3D functional spec). Removed duplicate shader addition of texture bias.
