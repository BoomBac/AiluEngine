# Texture sampling

## Ownership and binding

`Texture` owns the default `SamplerDesc`. GPU resource release/recreation preserves these authoring settings.
`FilterMode` and `WrapMode` remain compatibility accessors over the same descriptor. Defaults are trilinear/repeat,
matching the previous Standard Lit raster sampler. Texture assets persist defaults in `TextureImportSetting::_sampler_desc`.
Sampling changes do not change pixel data and are intentionally excluded from the texture pixel-artifact hash.

`Material` owns per-property `TextureSamplingSettings`: inherit texture defaults, override the complete descriptor,
and optionally disable automatic view mip bias. Overrides replace the texture sampler; they are not added to it.
Settings survive material copy/instances and are serialized on `AssetTextureBinding::_sampling`. Absent fields in old
assets retain defaults. GPU indices and the derived view mask are not serialized.

`D3DDescriptorMgr` owns an independent shader-visible SAMPLER heap and a mutex-protected cache of normalized native
states. Identical states reuse an index. Cache entries are immutable and remain alive until GPU-idle descriptor-manager
shutdown; no texture or material holds a descriptor allocation to free. This first implementation has a hard limit of
2048 distinct cached states per context lifetime and throws on exhaustion. There is no eviction or heap rollover.
Continuous global/view bias changes do not allocate sampler states.

Command buffers bind both the SRV/UAV and sampler heaps. Graphics, Compute and DXR root signatures expose
`g_material_samplers` at `s0, space11`. Fixed utility samplers remain at `s0..s7, space0`; their single definition is
`D3DStaticSamplers.h`. Comparison sampling remains on the fixed shadow sampler in this version.
Shader artifact keys include the sampler-binding ABI version so old root signatures cannot be hydrated silently.

## Material consumers

Standard Lit uploads six sampler indices (albedo, normal, roughness, metallic, specular, emission) and an automatic-bias
mask into each draw's material constants. Texture sampler revisions invalidate frame constant-block caches.
The transient Compute/DXR `MaterialData` has a 128-byte stride and five sampler indices; existing prefix offsets stay
unchanged. GI material and triangle-light emission evaluations use these samplers consistently.

Custom shaders can include `material_sampling.hlsli`, obtain indices through `Material::GetTextureSamplerIndex` or
`GraphicsContext::GetSamplerIndex`, and supply the indices in their own constants. Automatic material index upload is
currently the Standard Lit contract, not a general shader naming/reflection convention.

Default Compute 2D/array texture bindings expose all mips (and array slices). Explicit subresource bindings retain a
single mip/slice. This is necessary for mip bias and explicit LOD to work on ordinary Compute texture bindings.

## Bias policy

- `Renderer::SetTextureMipBias` / `GetTextureMipBias`: atomic manual offset, default zero, snapshotted into view constants.
- `Camera::SetAutoTextureMipBiasEnabled`: per-view switch, default true, persisted for scene cameras.
- Automatic DLSS bias: `log2(actual_render_width / output_width)`.
- Automatic FSR bias: `log2(actual_render_width / output_width) - 1`.
- Non-SR/resolved fallback: automatic bias zero. Provider selection alone does not activate the compensation.
- Raster material bias: manual plus automatic when the slot opts in; otherwise manual only.
- ShadowCaster: manual only. Probe captures do not use SR automatic bias.
- Compute/DXR material and GI sampling: explicit LOD plus manual bias; no raster SR footprint compensation.

The native sampler adds its own mip bias, including for `SampleLevel`; shaders must not add the texture bias again.
Explicit shader LOD/bias remains additive. Hardware performs final LOD/resource clamping.

## Original Object/BOX height normals

Standard Lit's `_NormalHeightParameters` is an optional per-material float4: signed strength, world-space distance,
BOX blend and mode (zero for existing tangent normals; one for source Object/BOX height; two for height plus RGB).
Modes one and two reuse the
Normal texture slot with explicit source sRGB/linear interpretation. The source coordinate frame is Z-up,
recovered as `(object.x, -object.z, object.y)` from imported Ailu geometry. Normal mode is stored in the material's
existing vector-property serialization; absent values remain zero. No new resource owner or texture slot exists.

Forward, deferred and voxel vertex paths carry local position/normal. `object_box_bump.hlsli` evaluates repeating
BOX projections with their source blend/orientation and derives the world-space surface gradient without using
atlas UVs or tangents. Raster view bias adjusts the sampled footprint; source texture sampler bias remains additive.
The Live Link importer selects anisotropic filtering for height maps. Unsupported height graphs retain baked normals.
Mode two also samples base color from this same original image when an opaque single Principled closure's RGB input
is exactly the height texture's Color output (including a zero-factor RGB Mix). It keeps the baked albedo as fallback
for existing Compute/DXR material consumers. Color adjustments, layered closures and linked alpha retain baking.

The transient 128-byte `MaterialData` retains its layout. It accepts only tangent normal handles, so Renderer omits
the Normal handle when a raster material uses BOX height mode. Current Compute/DXR consumers do not apply normal
textures; adding height evaluation there requires an explicit matching contract. This feature does not establish
general Blender closure/lighting parity.
Path tracing currently starts from LOD zero: negative bias cannot recover more detail without a ray-footprint baseline.

Camera frame preparation resets temporal history when effective bias changes. Path-tracer history includes manual bias
and texture sampler revisions. SceneView's Camera panel exposes Global Mip Bias, Auto SR Mip Bias and the effective value.
Manual renderer bias is runtime state; it is not currently persisted in EngineConfig.

## Validation

`SamplerSmoke` covers cache reuse and normalization, native GPU address/filter/LOD results, slot settings persistence,
legacy defaults, instance inheritance, resolved Camera policy/history resets, raster PNG/DDS capture and clean shutdown.
Run `Test/AnalyzeSamplerSmoke.py <run-directory>` to numerically validate the raster captures. Keep captures beside the
executable in a fresh directory. These checks do not replace DLSS/FSR temporal-quality or Editor interaction acceptance.
