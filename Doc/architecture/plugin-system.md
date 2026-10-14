# Plugin foundation

## Current implementation

M0–M6 provide `DynamicLibrary`, Plugin ABI v1, internal `PluginModule`, `PluginManager`, threaded lifecycle
tests, Editor lifecycle integration, manifest discovery/autoload and an Engine-owned registration scope.
The initial scope stores host records; menu, panel, dock and callback service APIs are separate follow-up work.

## Dynamic library ownership

`Engine/Inc/Framework/Platform/DynamicLibrary.h` owns one Windows loader reference without exposing `HMODULE`.
The object is non-copyable and non-movable. `Load` rejects replacing a loaded handle and records an absolute,
lexically normalized path. Missing files report the path and Windows error. Symbol lookup returns null for
unloaded libraries and missing/invalid names. `Unload` is idempotent, and the destructor calls it.
If `FreeLibrary` fails, ownership/path are retained for diagnosis and a possible retry.

This wrapper owns only the library reference. Its caller must destroy DLL instances and release all DLL
function pointers, descriptors and callbacks before unload. Operations on the same wrapper require caller
serialization; it has no internal synchronization. Logging uses `LogMgr`, which must already be initialized
and remain alive for failure reporting.

Long-lived Engine resources must be constructed by Engine APIs before they are retained by assets, scenes or
other Engine systems. A plugin may prepare/import resource data, but must not create a plugin-local
`shared_ptr` control block for an object that can outlive the plugin; its deleter/control-block code can reside
in the plugin DLL after that DLL is unloaded. Plugin-owned temporary objects must be destroyed during
`OnUnload` before `FreeLibrary`.

## ABI v1

`Engine/Inc/Framework/Plugin/PluginApi.h` defines four fixed C exports with explicit Windows `__cdecl`:
`AiluPluginGetApiVersion`, `AiluPluginGetDescriptor`, `AiluPluginCreate`, `AiluPluginDestroy`.
The version export must be resolved and checked before descriptor or object access.
Plugin producers define `AILU_BUILD_PLUGIN` to enable exported declarations; hosts use only the function
pointer aliases and dynamic discovery, so their modules receive no plugin export directives.

The descriptor uses DLL-owned static C strings. The host copies id/name/version before unload and does not
retain the raw descriptor. `PluginContext` provides the API version and a borrowed `IPluginHost` pointer.
The v1 host interface offers logging and a main-thread query; an optional side interface supplies progress reporting. It must outlive plugin use, including `OnUnload`.
The plugin interface offers `OnLoad`, `OnUnload` and `Tick`; no exception may cross the DLL boundary.

Concrete objects are allocated and destroyed inside the plugin DLL through Create/Destroy. The host never
directly deletes them. Required order: stop Tick/new registrations, release host registrations, OnUnload, Destroy,
null instance/descriptors/export pointers, FreeLibrary.
There are no reflected plugin types or dependencies on Editor private implementation.

The exported names are C discovery symbols, but descriptors and virtual interfaces still use a C++ ABI.
v1 requires compatible compiler/toolchain, architecture, runtime configuration and structure layout.
Increment `kPluginApiVersion` for incompatible layout, virtual-interface or calling-convention changes.

## Validation

`DynamicLibrarySmoke` uses a dedicated probe DLL to check loader ownership, symbol lookup, errors, explicit
and destructor unload, and `GetModuleHandleW` absence. `PluginSmoke` loads the minimal `AiluPluginSmoke` DLL
from `Plugins/SmokePlugin` relative to its executable, checks ABI discovery/lifecycle and host-owned string
copies after unload. These runners initialize logging only; they do not initialize Editor or Graphics.
The smoke plugin owns an atomic stop flag and a worker thread. OnUnload stops and joins the worker before
Destroy; a still-joinable worker in the destructor is a contract violation. Test-only exports report worker/tick
activity and are not part of ABI v1. The standalone suite runs 100 load/Tick/worker-check/unload cycles and
checks module absence after every unload. The bad-version fixture exports version only and is rejected first.

## PluginManager ownership and threading

`PluginManager` owns a small ordered vector of internal `PluginModule` objects and a narrow host implementation.
Linear ID lookup keeps v1 simple; the owning vector is also the explicit load order, reversed during shutdown.
Modules first load, validate version/exports and copy the descriptor; duplicate IDs are rejected before Create
or OnLoad. The manager reserves its entry storage before activation. Every failed candidate is RAII-cleaned.
If OnLoad has started but returns false, cleanup still calls OnUnload before Destroy and FreeLibrary.

Create/OnLoad/Tick/OnUnload/Destroy run only on the manager's owner thread. Construct, initialize, query, use
and destroy the manager on that thread. Wrong-thread operations are rejected before reading mutable state.
Lifecycle operations and recursive Tick are rejected while the manager is already executing a plugin call;
there is no deferred lifecycle queue. Destruction on another thread or during a plugin call is a contract violation.
The host and logging runtime must outlive all plugin callbacks and destruction.

## Editor integration

EditorApp owns a PluginManager initialized after successful Application initialization. An Editor-compiled
before-update delegate calls Tick with `TimeMgr::s_delta_time`. Finalize unsubscribes that delegate, shuts
down and destroys the manager before saving Editor state, stopping other Editor services and finalizing Application.
The Engine layer has no dependency on Editor implementation. Editor scans `<AiluRoot>/Plugins` followed by
`<ProjectRoot>/Plugins`, then explicitly loads only manifests marked `autoload: true`. Player autoload is absent.

Editor supports explicit lifecycle-validation options after the project argument: `--plugin-smoke <DLL>` and
`--exit-after-frames <positive count>`. The latter posts WM_CLOSE and uses the normal application exit path.
The former manually loads a test DLL and checks activity before shutdown and module absence afterward.
`--plugin-smoke-autoload <DLL>` checks the same fixture after startup autoload, without manually loading it.
Both test modes create a host record and verify it is active before shutdown; scope release precedes OnUnload.
`Test/RunPluginEditorSmoke.ps1` runs empty and threaded cases with a timeout, preserves config/layout/session
bytes in finally, and retains independent logs. Existing non-plugin scene/resource errors and allocator warnings
are not suppressed or interpreted as plugin acceptance failures; compare the two logs when assessing regressions.

## Manifest discovery

`PluginManifest.h/.cpp` uses RapidJSON directly without reflected types or ObjectRegister/asset initialization.
`ScanPlugins(roots)` accepts ordered roots, scans only immediate child directories for `plugin.json`, sorts
candidates within each root and keeps the first duplicate ID. Missing roots are normal; malformed/unreadable
candidates produce path/reason diagnostics and do not prevent other entries from being discovered.
Scanning does not load DLLs. `DiscoveredPlugins()` returns an Engine-owned snapshot.

The strict v1 schema has exactly seven fields: id, name, version, api_version, type, library and autoload.
Strings must be nonempty without embedded NUL; api_version must be a compatible uint32; type is runtime/editor;
autoload is a boolean. Library paths must be relative and remain within the manifest directory, including
symlink resolution. The `{config}` token in a library path expands to the lowercase active CMake configuration
(for example, `debug` or `relwithdebinfo`) before path validation. There is no dependency graph, override priority
or engine-version resolution.

`LoadPlugin(String id)` loads a discovered entry. `LoadAutoloadPlugins()` tries all marked entries, preserving
successful loads while reporting failures; repeated calls skip already loaded IDs. Manual path loading is
still available. DLL API version remains authoritative; id/name/version/type must match the manifest before
Create/OnLoad. All failures retain the original rollback and unload ordering.

## Owner registration scope

`PluginRegistration.h` defines Engine-only value tokens, handles and records; ABI v1 is unchanged. Tokens
are issued by an atomic identity counter, unique process-wide for each load attempt and never recycled.
This counter owns no subsystem or registry. Handles combine owner token with a non-reused per-module counter;
stale/foreign owners cannot register or affect another owner after reload, reinitialize or across managers.

PluginManager exposes GetPluginToken, RegisterHostRecord, ReleaseRegistration, ReleaseAllRegistrations,
HasRegistration and Registrations (metadata snapshot). Registration operations require the owner thread and
an active module outside a manager call. The initial typed scope stores only Engine-owned strings and POD
metadata. There is no arbitrary cleanup function, std::function, plugin-owned closure manager or opaque
plugin destructor in cleanup records. Future service implementations must add typed Engine-owned detach
logic before exposing callback/menu/panel registration; this record scope does not reclaim arbitrary delegates.

Each PluginModule owns its registration scope. Unload first enters the unloading state and closes/releases
the scope, then calls OnUnload/Destroy and clears exports before FreeLibrary. This also runs on failed-load
RAII cleanup. Owner lookups no longer yield an active token during teardown. Tests query the registry inside
OnUnload and Destroy log observations, proving records are absent before plugin teardown, not only afterward.

## Current acceptance

Standalone M0–M6 runners pass, including M3 threaded x100 and M5/M6 rejection/order/isolation tests. The live
Editor runner's `-TestManifest` option additionally creates a temporary project manifest, checks discover-only
and autoload cases and removes only its verified fixture path in finally. See the completed plugin-system
execution plan for exact evidence, commands and existing non-plugin Editor error/warning limitations.

## Blender Live Link material conversion

Pure Diffuse BSDF closures (including mixtures with Transparent/Emission but no reflective closure) are approximated
as Standard Lit with roughness=1 and metallic=0. Direct and native-baked exports omit roughness/metallic texture paths
for this case: Blender's Diffuse roughness controls diffuse scattering rather than GGX reflection width.
Standalone Glossy, Principled, Glass and mixtures containing Principled/Glass closures retain their existing conversion.

Diffuse + Glossy mixtures containing only these closures and Transparent/Emission helpers use separate normalized
lobe weights: diffuse pigment comes only from Diffuse closures, and GGX roughness only from Glossy closures.
Glossy mixture weight does not become metallic. Native channel graphs evaluate nested Mix/Add, node group inputs
and spatial texture weights. Zero reflection weight falls back to roughness 1; zero diffuse weight uses black
diffuse pigment. Independent reflection strength is not transported; Standard Lit remains an approximation.

The Editor plugin listens on loopback port 17777 by default. `AILU_BLENDER_LIVE_LINK_PORT` optionally selects
a process-local port (1 through 65535) for isolated diagnostics; invalid values retain the default. The headless
BMW exporter honors the same override. This does not change installed Blender preferences or protocol v1.

For an Add Shader whose primary closure is Glass/Refraction, channel baking retains that closure's color,
roughness and zero metallic value. Auxiliary Glossy pigment/metalness is not averaged into glass transmission;
coverage and emission retain their existing composition. Additional reflection layers remain an approximation.

The Blender add-on owns complex surface evaluation on Blender's main thread. `material_bake.py` creates isolated
CPU Cycles scenes and evaluated meshes; `mesh_sync.py` transmits the bake/source UVs with V converted to `1 - V`
for the engine's top-origin image sampling. Tangent handedness changes with that UV reflection.
AUTO exports supported primary image branches as shared PBR materials, retaining source UVs and reporting
coordinate/layer adjustments as approximations. Unresolved graphs bake at 256px using existing UVs when available;
their overlap may approximate spatial variations. Explicit `ailu_export_mode=BAKE` uses native graph evaluation,
unique atlases and a 1024px default. Baked coordinate-sensitive graphs and modifiers keep object-specific identities.
Full Sync creates object identities first, then streams each object's material/mesh packets consecutively; host
material updates already rebind pending object GUID references. Four LRU mesh entries bound cache lifetime.
Channel materials/images are released after each pass. Clean file image buffers newly loaded by export are freed;
preloaded/dirty/generated user buffers are retained. Cache invalidation and shutdown release temporary meshes. Source
scene/view-layer state is restored, and incremental synchronization waits for Object Mode.
Full/incremental exports suppress their own depsgraph notifications and explicitly drain deferred view-layer
updates before restoring change tracking. Pending real edits are processed before entering suppression; no
post-export time window discards user edits. FullSync end closes one batch; later user edits are still incremental.
Incremental material-only edits coalesce for 450 ms before the main-thread Cycles bake. Each bake batch writes channel
PNGs to unique temporary paths so queued material packets cannot read a later bake's overwritten files.
Node cloning copies instance settings only; RNA registration properties and `bl_*` metadata are excluded because
their setters modify shared Blender node types. Source socket collections are snapshotted before recursive cloning.

The add-on's existing main-thread timer also owns its nonblocking TCP socket. Connect/handshake, fragmented reads,
partial writes and reconnect advance through `LinkConnection.poll()`; packet generation polls between messages
so lengthy Full Sync callbacks can still advance sends. Socket writes have a per-poll time/byte budget. Disconnect
closes the socket and discards pending packets/diagnostics. No persistent Python network thread runs alongside
Blender operators or Cycles; Blender's Python integration does not support that concurrency.

Mesh encoding uses typed arrays and Blender foreach_get rather than per-corner Python tuples/floats. One vertex per
mesh loop preserves split attributes; triangles reference these vertices instead of duplicating every corner.
Wire layout, basis conversion, bounds and material grouping remain protocol v1. Missing/failed tangent generation
uses a deterministic tangent default rather than reading uninitialized loop tangent data.

Evaluated mesh instances are imported as root children using their depsgraph world transforms. Session, instancer,
source GUID and persistent instance path determine stable UUIDs. The protocol-v1 handshake negotiates the
`instance_batch_v1` and `mesh_prototype_v1` capabilities. Instance-batch packets carry up to 128 object records
and reference the source object's mesh/material slots. Scene objects send their mesh with the ordinary mesh
packet. A mesh prototype outside `scene.objects` sends its material data and a prototype-only mesh packet before
its instance batches; the Engine persists and caches that mesh without creating a scene entity. Generated instances
do not resend geometry. Placement-only updates send changed instance records, source edits refresh affected
instances after the source mesh update, and removed or empty-geometry instances send deletion messages. The Engine
reuses the prototype `Render::Mesh` for each instance. Software ray-tracing triangles and BLAS
nodes are packed once per unique mesh, while entity/submesh maps retain per-instance lookups. Active scene primitives
support up to 65,536 entries with TLAS node storage for that many primitives; the legacy RenderPipeline node storage
remains 2,000.
Original objects use view-layer visibility; mesh camera visibility is approximated by entity visibility, including
camera-invisible emitter helpers. This also removes those meshes from GI/shadows; independent ray visibility and
per-instance world-coordinate material rebaking are not implemented. Non-TRS shear remains outside transform transport.

Material messages optionally carry `alpha_blend`, `alpha_test`, `double_sided` and `emission_texture_factor`.
The host uses existing Standard Lit surface/cull APIs. Explicit empty texture paths remove old bindings; absent
fields retain the previous protocol-v1 behavior. Live Link continuous opacity opts into `LINEAR_ALPHA`; emission
textures opt into `EMISSION_TEXTURE_FACTOR`. Existing material assets retain their shader keyword defaults.

Native value/coordinate graphs are baked, but shader lobes remain Standard Lit approximations. Camera-ray selection
and view-dependent effects become static; world lighting and volume transport are not implemented by surface
material messages. Volume-only helpers export an invisible surface instead of an opaque checker fallback.

## Blender Live Link progress reporting

PluginManager implements an optional IPluginProgressHost side interface, discovered through RTTI from IPluginHost.
The existing ABI v1 host vtable and PluginContext layout remain unchanged; compatible C++ toolchains remain required.
EditorApp supplies a host-owned callback that copies strings into EditorProgressHandle state, retaining one handle per title.
The Engine host checks owner-thread affinity and contains callback exceptions. No Editor types or plugin closures cross the ABI.
Blender Full Sync begins one non-cancellable indeterminate task, reports apply stages and packet counts, and terminates on End,
disconnect, overflow, replacement Begin or unload. LiveLinkSession exposes the last Apply result so failed data does not report
successful completion. Begin yields the current Tick; subsequent Full Sync batches yield after approximately 32ms
between commands. Incremental updates retain the 4ms budget. The diagnostic environment option
`AILU_BLENDER_LIVE_LINK_FULL_SYNC_BUDGET_MS` accepts 4 through 64ms, sampled at plugin load.
Individual synchronous Apply calls remain non-interruptible. Incremental updates do not create progress cards. The transport
and Blender add-on protocol remain unchanged; this task is not a percentage of Blender bake or network byte completion.

The Blender add-on disconnects on Full Sync export exceptions without sending End, so the host ends the interrupted
progress task and preserves the partial scene for a later full resync. Operator errors are reported and the timer
catches export errors without unregistering itself. Collection mesh geometry is encoded before material export;
empty evaluated meshes skip surface baking and empty meshes without UVs skip atlas projection.

LiveLinkSession shares immutable Mesh resources for exactly equal protocol geometry bodies (excluding object GUID
and revision). The session-owned cache uses sampled fingerprints only for bucket selection and compares every byte
before reuse, including bounds, vertex attributes, index buffers and material-slot mapping. Entity transforms and
material bindings remain independent; geometry updates publish a new Mesh rather than modifying a shared Mesh.
Cache entries hold weak Mesh references and at most 128 MiB of canonical payload bytes. Expired entries are pruned
on publish, delete and completed sync; reset/unload clears the cache. Mesh CPU derivation and GPU upload retain their
existing main-thread ownership. `AILU_BLENDER_LIVE_LINK_DISABLE_MESH_CACHE=1` selects the uncached import control for
diagnostics without changing protocol v1 or Blender preferences.

Native Blender graph baking groups uncached surface materials on an object by resolution, keeping one temporary
scene context per batch. Each slot retains its own image targets, including isolated targets for non-batched slots.
Duplicate slots share their material target. A batch contains at most four materials and caps estimated retained
pixel arrays at 128 MiB; a single oversized material uses the existing single-material path. Alpha, GGX roughness and
metallic are evaluated through the previous red-channel semantics and packed into a single RGB emission
bake, then written to the existing independent texture files. Color, emission, tangent-space normal and
coverage retain separate passes. Diffuse-only closures omit roughness/metallic graph evaluation and files
that the transport would discard. Resolution, Cycles samples and per-pass image/material cleanup remain
unchanged; context restoration runs before removing the temporary scene, including export exceptions. Batch
preparation populates the existing material payload cache; material packets retain their previous slot order.
When every material in a batch has provably zero emission, the emission pass is omitted. The exporter retains
the independent emission PNG and copies the color EMIT target's raster alpha into zero RGB pixels. This leaves
normal/coverage baking and pass order unchanged. Closure defaults and zero-only Mix/Add branches are
recognized conservatively; unknown, spatial or nonzero emission retains native baking. PNG diagnostics separate
pixel preparation from encode/write, and source image diagnostics record newly loaded buffers released and
scan/free costs; those costs do not isolate decoding performed inside Cycles.
For primitive closures with unlinked finite alpha/roughness/metallic defaults, the packed scalar pass can also
be omitted if every material in the batch qualifies. Scalar reconstruction is deferred until the existing
coverage pass completes, multiplying coverage RGB by the defaults. Layered or linked scalar channels retain
native baking. Exported independent scalar files, material factors and opacity classification stay unchanged;
native shader arithmetic and reconstruction can differ by one 8-bit quantization level. Alpha defaults near
the 0.02/0.98 opacity thresholds retain native baking to avoid rounding changing the classification.

FullSync owns an explicit Blender ExportSession inside dirty_tracker export suppression. It acquires the
source evaluated depsgraph once for the fixed snapshot and releases the reference on completion/failure;
incremental exports continue acquiring their current depsgraph. Object bake calls use temp_override without
switching the window scene. UV atlas Edit Mode operators retain window switching and finally restoration.
All Blender RNA and bake calls stay on the main thread. Temporary-scene reuse remains a diagnostic option,
disabled by default; repeated context/depsgraph work was more significant than scene creation itself.

On Windows with Blender's bundled Python available, FullSync starts one hidden PNG encoder process. Blender
performs the existing color conversion and copies quantized top-to-bottom RGBA8 bytes through a bounded pipe.
The worker uses only Python standard-library PNG/zlib encoding, atomic file replacement and ready markers;
it never imports bpy. Material payloads wait for their files and are drained before FullSync End. Failure
disconnects the incomplete snapshot, invalidates its bake payloads and closes/reaps the process in finally.
Native image saving remains the fallback when the automatic worker cannot start or is unavailable; incremental
exports keep native saving. The worker is scoped to the sync and owns no persistent Blender data or preferences.

Diagnostic environment controls are `AILU_BLENDER_BAKE_CONTEXT_OVERRIDE_ONLY` and
`AILU_BLENDER_BAKE_REUSE_DEPSGRAPH` (default 1), `AILU_BLENDER_BAKE_PNG_PROCESS` (default AUTO; 0/1 force
native/process), `AILU_BLENDER_BAKE_REUSE_SCENE` (default 0), and `AILU_BLENDER_BAKE_DEVICE` (default CPU;
CUDA/OPTIX diagnostic NVIDIA device selection). GPU diagnostics restore device preferences without saving
them. Only CUDA was measured in the pipeline task; it was slower and failed strict material-factor equality.
Session diagnostics separately record context restoration, source graph acquisition and atlas evaluation;
parent process CPU time excludes the independent encoder and must not be reported as total pipeline CPU work.

## Blender Live Link persistence and reconnect

Imported entities are durable scene entities. Optional tag source scene/object GUIDs identify the Blender association;
engine persistent entity GUIDs remain independent when sources collide. Old scene documents default to unlinked.
The root carries only the source scene GUID. Plugin unload, disconnect and scene switches discard runtime tracking
without deleting persisted content. Begin rebuilds tracking from the active scene and resets per-connection revisions.
A complete successful snapshot reparents retained objects before deleting unseen objects of this source; interrupted
or failed snapshots never prune unseen objects. Different source scenes retain separate roots.

Meshes, materials and copied texture sources are registered/saved under project://BlenderLiveLink/<source-scene-guid>/.
The Engine creates procedural Mesh objects from the plugin's transferred CPU geometry; the plugin does not create
the long-lived Mesh shared_ptr. Meshes use immutable assets when geometry changes, so previously shared objects can
diverge safely. Validated equal
incoming geometry reuses a loaded mesh without rebuilding or resaving it. Old mesh assets are retained; no automatic
orphan deletion is performed. Texture documents preserve color/data/normal import settings and refer to project copies,
not Blender bake cache paths. Blender owns imported transform/hierarchy/visibility/geometry/material fields; engine-side
overrides are not tracked in this version.

Blender sends a full snapshot after reconnect, source scene switch or file load, then uses dirty incremental updates.
New/duplicated objects repair GUID collisions before membership comparison. Protocol v1 is unchanged: reconnect still
transmits full geometry/material payloads; hash negotiation and offline delta replay are not implemented. Save both the
.blend file (source identities) and engine scene after completed sync. Switching the active engine scene while connected
requires a Full Sync to establish its new baseline.

## Blender Live Link camera projection

Camera entities keep the ordinary orientation-preserving Blender-to-engine object transform and hierarchy. The
camera interprets converted local forward (0,-1,0) and up (0,0,-1); projection X is reversed to preserve Blender
screen-right rather than mirror the image. These optional Camera settings survive scene/prefab save/load.

The add-on derives actual frame extents from Camera.view_frame(scene): perspective extents at unit depth give
vertical FOV and NDC lens offsets; orthographic extents give width and offsets. This includes Sensor Fit, portrait
frames and pixel aspect. Protocol v1 CAMERA_UPSERT adds optional fov_y_degrees and projection_offset fields.
The host converts legacy fov_degrees (horizontal) to vertical when the new field is absent. Imported camera aspect
is locked against output-size changes. The dirty tracker refreshes cameras when render dimensions/pixel aspect change,
even without an object depsgraph update. Panoramic cameras, cropped render borders, DOF and general sheared rigs are not covered.
