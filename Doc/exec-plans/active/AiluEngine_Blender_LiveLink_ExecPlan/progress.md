# AiluEngine Blender Live Link — Progress

> Status: Active
>
> Current Milestone: M6 implementation complete; final runtime acceptance pending
>
> Last Updated: 2026-10-04
> Last Updated Baseline: AiluEngine_Source(20260930-154655)

---

# Purpose

这是 Blender Live Link 长任务的唯一进度与恢复入口。

上下文压缩、换模型、换会话后：

```text
先读本文件
```

不要重新从整个仓库推断已经做过什么。

---

# Scope

本文件维护：

- 当前里程碑。
- 每个 milestone 状态。
- 当前 next action。
- 已执行 validation。
- Decision Log。
- Surprises & Discoveries。
- Blockers。
- Known Follow-ups。
- Outcomes。
- Resume Protocol。

---

# Non-goals

本文件不重复：

- 系统完整架构。
- 每个 milestone 的全部实现细节。

这些内容分别在：

```text
README.md
milestones/*.md
```

---

# Architecture Constraints

长期不变量：

1. Live Link 是现有 Plugin System 上的 Editor Plugin。
2. `kPluginApiVersion` MVP 保持 1。
3. `OnLoad()` 不访问 Scene/Graphics。
4. network thread 不访问 ECS/Render resource。
5. Engine mutation 只在 plugin Tick/main thread。
6. loopback-only。
7. Blender UUID 是稳定 identity。
8. Live Link entities 必须 transient。
9. Static Mesh 走 custom binary payload。
10. Material 只支持 Principled subset。
11. Texture 使用 external path/temp export，不写 Project Asset。
12. Full Sync 是 authoritative recovery。
13. Play Mode 不同步 runtime scene。
14. plugin unload 前必须 stop/join network worker。
15. 一个 milestone Validation 未通过，不进入下一个。

---

# Baseline

已对上传的最新源码包完成架构检查。

确认当前仓库已经有：

```text
DynamicLibrary
PluginApi
PluginManager
PluginManifest
PluginRegistrationScope
Editor plugin scan/autoload/tick/shutdown integration
```

因此 Blender Live Link 可以直接进入插件实现，不需要先执行旧的 Plugin System M0~M6。

---

# Target Outcome

最终：

```text
Blender Add-on
        ↕ loopback TCP
AiluBlenderLiveLink.dll
        ↓
Transient Ailu Scene
```

同步：

```text
Static Mesh
Material
Texture
Light
Camera
Transform/Hierarchy
```

---

# Relevant Files

当前最重要：

```text
Engine/Inc/Framework/Plugin/PluginApi.h
Engine/Inc/Framework/Plugin/PluginManager.h
Engine/Src/Framework/Plugin/PluginManager.cpp

Editor/Src/EditorApp.cpp

Engine/Inc/Scene/Scene.h
Engine/Inc/Scene/Component.h
Engine/Src/Scene/Scene.cpp
Engine/Src/Assets/AssetHandlers.cpp

Engine/Inc/Render/Mesh.h
Engine/Src/Render/Mesh.cpp

Engine/Inc/Render/Material.h
Engine/Src/Render/Material.cpp

Engine/Inc/Framework/Common/ResourceMgr.h
Engine/Src/Framework/Common/ResourceMgr.cpp

Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp
```

---

# Milestones

## M0 — Plugin Scaffold + Loopback Transport

Status: IMPLEMENTED; runtime acceptance pending

```text
CURRENT
```

File:

```text
milestones/m0-plugin-transport.md
```

Progress:

- [x] AiluBlenderLiveLink target 编译通过。
- [x] plugin.json，autoload=true。
- [x] loopback TCP server source。
- [x] worker start/stop/join 和 socket shutdown source。
- [x] 固定 little-endian protocol header。
- [x] HELLO/HELLO_ACK 与 PING/PONG source。
- [x] magic/version/payload length/truncated payload guard source。
- [ ] Editor autoload 和 live bind 验收（尝试启动；运行环境用户目录指向 `CodexSandboxOffline`，与源码根目录不一致，插件无法按既有扫描路径发现）。
- [ ] HELLO + 1000 次 PING/PONG smoke（transport smoke client 已提供，未能连接 Editor）。
- [ ] malformed client cases（未能连接 Editor）。
- [ ] active-client plugin unload、reload 和 port rebind 验收（未能连接 Editor）。

---

## M1 — Session Identity + Transient Scene + Transform

Status: IMPLEMENTED; Blender/Editor acceptance pending

```text
NOT STARTED
```

File:

```text
milestones/m1-session-transform.md
```

Progress:

- [x] generic transient entity semantics（TagComponent runtime-only flag）。
- [x] Blender addon skeleton / persistent UUID / session root / Full Sync / create-delete / hierarchy / transform。
- [x] Scene Asset save exclusion。
- [ ] coordinate golden tests and Blender/Editor scene acceptance（Blender unavailable）。

---

## M2 — Static Mesh

Status: IMPLEMENTED; Blender/Editor acceptance pending

```text
NOT STARTED
```

File:

```text
milestones/m2-static-mesh.md
```

Progress:

- [x] evaluated mesh / binary payload / runtime Mesh construction。
- [x] normals/tangents/UV / submesh grouping / bounds / debounce。
- [ ] large mesh validation。

---

## M3 — Material + Texture

Status: IMPLEMENTED; Blender/Editor acceptance pending

```text
NOT STARTED
```

File:

```text
milestones/m3-material-texture.md
```

Progress:

- [x] Principled material / scalar PBR / BaseColor and Normal textures。
- [x] RM packing / packed and generated image export / material slots / transient texture cache。
- [ ] image and material visual validation。

---

## M4 — Light + Camera

Status: IMPLEMENTED; Blender/Editor acceptance pending

```text
NOT STARTED
```

File:

```text
milestones/m4-light-camera.md
```

Progress:

- [x] Sun / Point / Spot / rectangular Area light mapping。
- [x] intensity mapping / Perspective and Orthographic camera mapping。
- [ ] semantic orientation validation。

---

## M5 — Incremental + Reconnect

Status: IMPLEMENTED; Blender/Editor acceptance pending

```text
NOT STARTED
```

File:

```text
milestones/m5-incremental-reconnect.md
```

Progress:

- [x] depsgraph dirty collector / debounce / deletion detection。
- [x] coalescing / bounded queue / reconnect and full resync。
- [x] play mode and scene-switch recovery paths。
- [ ] continuous edit and recovery stress validation。

---

## M6 — Stability + Package

Status: IMPLEMENTED; final acceptance pending

```text
NOT STARTED
```

File:

```text
milestones/m6-stability-package.md
```

Progress:

- [ ] stress / metrics / memory cleanup validation。
- [x] Blender addon zip built and package layout inspected。
- [x] Ailu plugin manifest and Debug DLL deploy layout present。
- [x] user README and PowerShell transport smoke client。
- [ ] final Editor + Blender acceptance。

---

# Current Milestone

```text
M6 implementation complete; Editor/Blender runtime acceptance pending
```

当前 Next Actions：

1. 在具有一致用户目录与源码根路径的 Windows 环境启动 Editor，完成 M0 transport smoke。
2. 复测 Blender Cube 全量同步，确认 WorldOutline 中 Cube 子节点、Static Mesh 可见和重复同步稳定。
3. 在 Blender 中安装 Add-on 并完成 M1–M5 场景、资源与恢复验收。
4. 完成 M6 压力、内存、性能和视觉验收后再将各 milestone 标为 PASS。

---

# Progress

当前完成：

- [x] 读取最新源码 snapshot。
- [x] 确认 Plugin System 已实现。
- [x] 确认 Editor 已接 Plugin lifecycle。
- [x] 确认 Plugin `OnLoad` 早于 Scene Open。
- [x] 确认 Scene PersistentId/Guid API。
- [x] 确认 Scene Reparent/Add/Remove API。
- [x] 确认 Mesh 可直接 runtime 构造。
- [x] 确认 Material 标准 PBR 参数。
- [x] 确认 Texture 有 `LoadExternalTexture`。
- [x] 确认 Light 类型和 Camera API。
- [x] 选定 custom binary Mesh。
- [x] 选定 transient Live Link subtree。
- [x] 选定 Principled-only Material MVP。
- [x] 选定 loopback TCP。
- [x] 完成 ExecPlan 拆分。
- [x] 完成 M0–M6 插件、协议、session、资源同步、增量同步和 Add-on 源码实现。
- [x] 完成 `cmake --build --preset build-debug`（2026-10-04；exit 0）。
- [x] 构建 Blender Add-on zip 并检查包内目录结构。
- [x] 修复 Full Sync 覆盖 `Connected` 状态的问题并重新打包；Full Sync 不再使面板误显示为未连接。
- [x] 根据 Blender 5.0.1 崩溃栈修复 evaluated mesh UV 读取：先批量复制 UV 数据，再计算 tangents，避免使用可能失效的 UV RNA collection 项。
- [x] 修复重复 Full Sync 根节点：在当前 Scene 复用一个 transient LiveLink root 并清理其余匹配根；C++ 编译通过，最终 DLL 链接因 Editor 正在加载该 DLL 而被 `LNK1168` 阻止。
- [x] 根据运行时诊断修复 64 位 ECS Entity 被 `unsigned int` 截断：LiveLink 根节点与实体映射统一使用 `ECS::Entity`；`AiluBlenderLiveLink` Debug target 构建通过。
- [x] ObjectUpsert 失败诊断改为仅输出 Warning，避免大型场景全量同步时逐对象刷屏。
- [x] 修复 LiveLink Mesh bounds 数量不满足 Renderer 约定的问题：生成整体 bounds 加每个 submesh 的 bounds，并初始化 StaticMeshComponent transformed bounds，避免 `Renderer.cpp` 对 `_transformed_aabbs[submesh_index + 1]` 越界。
- [x] 修复 Blender 5 全量同步 Sun 时的 Python TypeError：光线方向矩阵乘法改用 `mathutils.Vector`；重新生成 Add-on ZIP。
- [x] 修复 transport smoke client 的 PowerShell 多行条件语法；PowerShell AST parse 通过。
- [x] 尝试 Editor runtime acceptance；确认当前 sandbox 用户目录不匹配是阻塞原因，测试进程已停止且 Editor 配置已恢复。

---

# Latest Validation

通过：

```text
cmake --build --preset build-debug
git diff --check
BuildBlenderLiveLinkAddon.ps1 and ZIP package layout inspection
PowerShell AST parse for `livelink_smoke_client.ps1`
`cmake --build --preset build-debug --target AiluBlenderLiveLink`（exit 0；包含 ObjectUpsert 诊断日志）
`git diff --check -- Plugins/BlenderLiveLink`（exit 0）
`cmake --build --preset build-debug`（2026-10-05；exit 0，包含 Mesh bounds 修复）
`BuildBlenderLiveLinkAddon.ps1`（exit 0，包含 Blender Vector 类型修复）
```

尚未通过/无法执行：

```text
Editor autoload/live bind (startup path resolves under `C:\Users\CodexSandboxOffline\OneDrive\AiluEngine`, while this checkout is `C:\Users\BoomBac\OneDrive\AiluEngine`); after the latest DLL rebuild, the Blender Cube full-sync retest and diagnostic log review are also pending.
HELLO/PING-PONG and malformed packet smoke (no server listener)
active-client unload/reload and port rebind
Blender add-on syntax/runtime, scene synchronization, and visual acceptance (Blender and Python interpreter unavailable)
```

M0 runtime and M1–M6 Editor/Blender acceptance remain pending. M1–M6 source implementation proceeded because the user explicitly requested completion through M6 in one task; no runtime or visual gates are marked PASS.

---

# Decision Log

## D001 — 复用现有 Plugin System

日期：

```text
2026-09-30
```

决定：

```text
不重做 PluginManager
不 bump Plugin API
```

理由：

当前源码已完成动态 DLL、Manifest、Autoload、Tick、Unload、RegistrationScope。

---

## D002 — Live Link 使用 Editor Plugin

```text
EPluginType::kEditor
autoload = true (MVP)
```

MVP 没有 Ailu Plugin Manager UI，autoload 最实用。

---

## D003 — OnLoad 只启动 CPU/Network

原因：

Editor 当前 autoload plugin 早于 RenderPipeline/Scene Open。

---

## D004 — Loopback TCP

```text
127.0.0.1
default port 17777
```

不支持 LAN。

---

## D005 — Static Mesh custom binary

不经过 FBX/glTF 文件。

原因：

- runtime Mesh API 已足够。
- evaluated mesh 更直接。
- importer/material translation 不应进入实时热路径。
- 更容易控制 revision/coalescing。

---

## D006 — 第一版 Mesh flatten triangle corners

优先：

```text
UV seam
split normal
tangent correctness
```

后续再 dedup。

---

## D007 — Texture external transient load

普通 Image：

```text
absolute path
```

Packed/Generated：

```text
Blender addon temp file
```

Ailu：

```text
LoadExternalTexture
```

不写 Asset DB。

---

## D008 — Principled-only

不做任意 node graph。

---

## D009 — Live Link entity transient

原因：

当前 Scene save 否则会序列化 Live Link entity。

使用 generic Engine transient semantics，不使用 plugin-defined ECS component。

---

## D010 — Object GUID = Entity PersistentId

名字只作为 display name。

---

## D011 — Full Sync authoritative

任何 recovery 优先 Full Sync。

---

## D012 — Camera/Light 发 semantic direction

不复制 Blender quaternion。

---

## D013 — Play Mode suspend

不修改 runtime scene copy。

---

# Surprises & Discoveries

## S001 — Plugin System 已经是可使用状态

与旧任务包相比，最新仓库已经包含：

```text
PluginManifest
PluginRegistration
PluginManager
DynamicLibrary
Editor lifecycle integration
```

所以本任务起点大幅后移。

---

## S002 — Plugin Autoload 发生在 Scene Open 之前

这是 Live Link 生命周期设计的关键事实。

影响：

```text
OnLoad 不能 Create Entity
OnLoad 不能 Load GPU resource
```

---

## S003 — PluginRegistrationScope 当前不是完整 Editor Extension API

因此 M0~M6 不要求 Ailu Editor 内建 Live Link Panel。

Blender side 提供 UI。

---

## S004 — 当前 Scene Save 没有 Live Link transient exclusion

如果不改：

```text
Save Scene
```

会把预览 entity 写入项目。

M1 必须先解决。

---

## S005 — Direct Runtime Mesh 比现有 Parser 更适合 Live Link

当前 Render::Mesh 公共 API 已能直接构建。

无需引入文件 exporter。

---

## S006 — `ImportResource` 不应成为 Live Link transient 资源 API

不仅会污染 Asset 语义，而且当前实现路径也不适合作为这个实时模块的可靠返回对象接口。

使用：

```text
LoadExternalTexture
runtime Mesh/Material
```

---

## S007 — Camera FovH 是 degree

Blender camera API 输入常见为 radian。

M4 必须显式转换。

---

## S008 — Area Light 需要限制 shape

Ailu 当前 rectangle-style area world data 比 Blender shape 集更窄。

MVP exact support：

```text
Square
Rectangle
```

---

## S009 — Mesh basis reflection 会影响 winding/tangent

坐标系变换不仅是 position。

M2 必须实际验证：

```text
triangle winding
normal
tangent handedness
```

不能仅看模型“差不多对”。

---

# Blockers

当前无代码级 blocker。

实施前需要确认的唯一工程集成项：

```text
仓库 root 是否已有 Plugins 子目录和统一 plugin CMake helper
```

如果没有，创建最小：

```text
Plugins/BlenderLiveLink
```

不要因此重构顶层 CMake。

---

# Validation

## M0 Gate

```text
HELLO
PING
malformed packet
client kill
plugin unload while connected
reload
```

全通过才能进入 M1。

## M1 Gate

```text
identity
hierarchy
coordinate
transient save exclusion
```

全通过才能进入 M2。

## M2 Gate

```text
modifier
winding
normal
tangent
UV
submesh
large mesh
```

全通过才能进入 M3。

## M3 Gate

```text
PBR scalar
texture
normal
RM
material slots
```

全通过才能进入 M4。

## M4 Gate

```text
light direction
spot
camera forward
FOV
ortho
```

全通过才能进入 M5。

## M5 Gate

```text
long edit
delete
reconnect
queue bound
full resync
play-mode recovery
```

全通过才能进入 M6。

## M6 Gate

最终 acceptance matrix 全通过。

---

# Known Follow-ups

不进入 MVP：

- skeletal mesh。
- animation。
- bidirectional。
- remote machine。
- mesh compression。
- mesh dedup。
- instancing。
- advanced materials。
- HDRI/world。
- collection/prefab mapping。
- Editor-side plugin panel。
- photometric exact units。
- Ailu selection -> Blender selection。

---

# Outcomes

当前：

```text
M0 implementation in progress
Debug build passed; runtime acceptance pending
```

最终完成时修改为：

```text
AiluEngine Blender Live Link MVP complete
```

并记录：

- final commit。
- Blender tested version。
- Ailu build config。
- performance metrics。
- remaining known follow-ups。

---

# Resume Protocol

任何后续模型必须按此顺序恢复：

```text
1. progress.md
2. README.md 中与当前问题有关的架构章节
3. 当前 milestones/mX-*.md
4. 只打开当前 milestone Relevant Files
5. 开始实现/继续实现
```

禁止：

```text
每次恢复都重新全仓扫描
每次恢复重新设计协议
越过当前 milestone
```

每完成一个步骤：

1. 修改 Current Milestone checklist。
2. 把实际代码文件写入 Relevant Changes（如后续需要可增加该节）。
3. 写实际 Validation 命令。
4. 写 PASS/FAIL。
5. 新发现进入 `Surprises & Discoveries`。
6. 新架构选择进入 `Decision Log`。
7. 当前 milestone 全部 PASS 后再切换到下一 milestone。

---

# M6 Performance Optimization — 2026-10-05

## Changes

- Blender full-sync and dirty-update batches export each material GUID once.
- Roughness/metallic packing uses NumPy vectorization, caches identical inputs, and removes its temporary Image datablock after saving.
- Ailu texture cache shares resources by normalized path, texture content/color space, and file fingerprint across materials.
- Packed/generated Blender images are saved once until an image/material/node-tree update invalidates the cache.

## Validation

- `cmake --build --preset build-debug`: PASS; `AiluBlenderLiveLink.dll` linked.
- `Tools/Blender/BuildBlenderLiveLinkAddon.ps1`: PASS; rebuilt ZIP contains updated sync modules.
- Blender Python syntax/runtime and Pine Forest before/after timing: NOT RUN (Blender executable unavailable in this environment).
- M6 measured performance and large-scene runtime acceptance remain pending.

# External Texture Load Guard — 2026-10-06

The `LoadExternalTexture` overloads now reject unsupported extensions, invalid setting types, and null/failed parser results before calling `Apply()`. This prevents a null `Ref<Texture2D>` from reaching `GpuResource::SetCreatedFence()` and replaces throwing reference `dynamic_cast` with a checked pointer cast so invalid settings produce a logged load failure instead of `std::bad_cast`.

Validation: `cmake --build --preset build-debug` PASS. Editor runtime reproduction remains pending.

# M6 Transfer and Import Diagnostics — 2026-10-06

## Changes

- Blender full sync reports scene scan, material/object extraction, geometry export, generation time, object/mesh/material counts, and bytes plus `sendall`/queue-wait totals grouped by protocol message type.
- Slow per-mesh export and slow material preparation emit object/material name, payload size or texture-path count, and elapsed time.
- Ailu reports receive time, main-thread queue wait, Apply time, message counts/bytes by type, and slow individual Apply commands.
- Each Ailu texture-cache miss reports local file size, texture usage, load result, and total parser-plus-Apply time; exceptions include the path and exception text.
- Texture paths are sent over TCP; image bytes are read from the referenced local path by Ailu. The new logs keep those costs distinct.

## Validation

- `cmake --build --preset build-debug`: PASS; Blender Live Link DLL linked.
- `Tools/Blender/BuildBlenderLiveLinkAddon.ps1`: PASS; ZIP rebuilt.
- Python `py_compile`: NOT RUN; the installed Windows Store Python launcher cannot start in this environment.
- Blender runtime timing and Hidden Alley import: PENDING; no Blender executable is available here.
- M6 measured performance and Editor/Blender runtime acceptance remain pending.

# HDR and Invalid Image Robustness — 2026-10-06

## Changes

- Blender Live Link converts external EXR images to temporary PNG files before transfer, including files using compression that the engine's TinyEXR build cannot decode. Color EXRs are converted from linear Rec.709 values for PNG output; data maps retain their numeric channel values.
- Roughness/metallic packing now skips scalar images that remain empty after reload, handles unreadable pixel buffers, and only emits a packed texture when at least one source channel was copied.
- HDRParser now accepts both RGB and RGBA float buffers, copies decoded pixels into engine-owned memory, and frees decoder buffers with their matching allocator. Invalid dimensions/channel counts fail cleanly.

## Validation

- `cmake --build --preset build-debug`: PASS; Engine, Editor, and Blender Live Link targets linked.
- `Tools/Blender/BuildBlenderLiveLinkAddon.ps1`: PASS; rebuilt `Tools/Blender/dist/ailu_live_link.zip`.
- Python syntax/runtime and Hidden Alley Blender-to-Editor reproduction: NOT RUN; Blender 5.0 executable and bundled Python are unavailable in this environment.
- DWA-compressed EXR still cannot be decoded directly by the engine TinyEXR parser; Live Link avoids this by converting it inside Blender. Direct `HDRParser` use on unsupported DWA EXRs remains unsupported.

# Preserve Live Link Scene Across Transport Interruptions — 2026-10-06

## Findings and Changes

- A client disconnect and incoming-command queue overflow both called `Clear()`, which removed the transient Live Link root and therefore all imported scene entities. This explains the scene clearing observed after the client disconnected and reconnected.
- Disconnect/overflow now preserve current entities, meshes, materials, and textures, while setting `_needs_full_resync` and ending any interrupted sync. Reconnection's next full-sync `Begin/End` reconciles the existing scene against the new Blender snapshot.
- Rebuilt the Blender add-on ZIP; verified it contains the zero-dimension roughness/metallic guards from the prior fix.

## Validation

- `cmake --build --preset build-debug`: PASS; `AiluBlenderLiveLink.dll` linked.
- `Tools/Blender/BuildBlenderLiveLinkAddon.ps1`: PASS; rebuilt `Tools/Blender/dist/ailu_live_link.zip` and inspected packed `material_sync.py` for invalid-dimension and empty-channel guards.
- Targeted `git diff --check`: PASS.
- Editor reconnect and Hidden Alley runtime reproduction: PENDING; cannot run Blender/Editor scene sync in this environment.
- The reported 28.55 MB mesh took 780.21 ms in Apply; this is a performance warning but is not itself evidence of why the TCP client disconnected.

# TLAS Buffer Capacity on Large Live Link Reimport — 2026-10-06

## Findings and Changes

- Crash at `GPUBuffer::SetDataInternal` was the TLAS upload size assertion: `Scene` allocated 2000 `LBVHNode` elements while `kMaxRenderObjectCount` is 2000 and `BVHBuilder::Build(1)` can produce up to roughly `2N - 1` nodes.
- Scene now reserves `2 * RenderConstants::kMaxRenderObjectCount` TLAS nodes and asserts the builder output remains within that supported capacity.

## Validation

- `cmake --build --preset build-debug`: PASS; Engine, Editor, and Blender Live Link targets linked.
- Targeted `git diff --check`: PASS.
- Runtime reimport of the user's scene and GPU stress validation: PENDING; not run in this environment.

# Hidden Alley Missing Texture Export — 2026-10-09

## Findings and Changes

- Direct-image-only discovery dropped base color paths behind Hue/Saturation, Gamma, Mix and Moss_Global groups.
  Image discovery now follows reroutes and group socket identifiers, retaining the primary image branch through
  adjustments/layers. Mix factors are not selected as color sources. Approximations and unsupported branches are logged.
- Shader discovery follows the active Material Output through groups/reroutes. `floor_01_Main_02` uses a Principled
  inside its group; choosing the first root-level Principled previously selected an unused node.
- Linked base color images now use a white factor because Ailu multiplies image samples by `_AlbedoValue`, whereas
  Blender replaces the linked socket's default. RM packing respects RGB/alpha selection and writes Non-Color data.
- Normal layers/Bump retain a primary normal map where available. Height, color adjustments, procedural masks,
  world-space projection and Mapping transforms are still approximations, not a full shader graph translation.
- Packed/generated image export uses `save_copy` and restores path/format, preserving the source image configuration.

## Validation

- Blender 5.0.1 background export of `I:/Downloads/hidden_alley/ph_hidden_alley.blend`: base-color paths increased
  from 81 to 108; 27 additional materials retain their source image, including brick walls, wood walls and floor.
  Updated export produced 117 material payloads, zero exceptions and zero nonexistent output texture paths.
- `Tools/Blender/tests/material_sync_smoke.py`: PASS in Blender 5.0.1; covers layer versus mask selection, group
  routing, active grouped shader, normal/Bump fallback, scalar channels, linear RM, base factor, image state and cycles.
- `cmake --build --preset build-debug`: PASS after allowing the existing external F-drive build output directory.
- Add-on ZIP rebuilt and checked against source; targeted `git diff --check`: PASS.
- Evidence: `Saved/BlenderTextureValidation/{scene_nodes,before,after}.json`, `smoke.log`, `build-debug.log`.
- Fresh Blender-to-Editor sync, GPU texture binding and final visual acceptance: PENDING. No Editor was running;
  background export proves source discovery and file preparation only. Install the rebuilt ZIP and run Full Sync.

# Hidden Alley Foliage and Transparent Overlay Follow-up — 2026-10-09

## Findings and Changes

- `grass_medium_01.001`, `weed_plant_02` and both tree leaf materials route their grouped Principled through
  Mix Shader/Add Shader. Discovery now walks these shader branches with the correct group-instance context.
- Foliage uses separate alpha images or transparent/opaque Mix Shader factors. The add-on packs the mask into
  base-color PNG alpha, handles either transparent-branch direction, and sends optional `alpha_test`, `alpha_cutoff`
  and `double_sided` fields. The host applies existing Standard Lit AlphaTest and cull APIs, including reset to opaque.
- `Material.001` on `decal_plane_mural` and `decal_plane_HavenAuto` actively outputs only Transparent BSDF; the old
  fallback picked an unused Principled and made these planes opaque. Pure transparent outputs now use zero alpha
  with clipping, preserving their invisibility instead of obscuring surfaces behind them.
- Shader layering/translucency remains an explicit approximation. This adds foliage cutouts, not full alpha blending.

## Validation

- Blender 5.0.1 full Hidden Alley export: 121 material payloads, 112 base-color paths, zero exceptions and missing files.
  Grass, weeds, both leaf materials, ivy and potted leaves all export alpha cutouts; `Material.001` exports alpha zero.
- Blender smoke PASS: layered shaders inside groups, separate alpha packing and RGB preservation, reversed opacity,
  pure transparent output priority, plus the prior image/RM/routing regression gates.
- `cmake --build --preset build-debug`: PASS, including updated `AiluBlenderLiveLink.dll`.
- Editor GPU validation: first attempt exceeded the 120-second startup wait while shaders were being prepared.
  All nine backed-up configuration/session files were restored; only the test process was stopped. A bounded retry
  with a 360-second startup wait uses a fresh evidence directory beside Debug `Editor.exe`.
- Export/smoke/build evidence: `Saved/BlenderTextureValidation/followup-20261009/`.
- Add-on ZIP rebuilt, bytecode excluded, packaged `material_sync.py` checked against source; targeted diff check PASS.
- GPU acceptance remains FAILED / NOT ACCEPTED: runs 02-05 transported four sets of three 1280x704 GBuffer PNGs.
  Run 02 could not read the sandbox-created temp texture. Runs 03-05 used workspace copies and logged successful
  albedo/normal/RM imports, but the sentinel-colored test geometry was not visible as expected; run 05 produced
  thin edge-like output despite inspected entity local transforms matching the requested pose. The image gates
  did not establish alpha cutout or overlay invisibility. No general rendering change was made on this evidence.
  Final run 05 has three consecutive `engine_frame` values per case and finite pixels, which are transport/integrity
  evidence only. All nine backed-up configuration/session files were restored after every run.
- GPU evidence is beside the exercised executable at
  `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderAlphaValidation-20261009-{01..05}/`.
  Full Hidden Alley runtime visual acceptance remains open; restart the updated Debug Editor and reinstall the ZIP.

# Hidden Alley Material Graph Coverage Repair — 2026-10-09

- Added native CPU Cycles channel baking for complex surface graphs, including nested shader groups/Mix/Add,
  group defaults, scalar masks, color adjustments, Mapping/UV/world coordinates and procedural Bump. Bake atlases
  and material identities are coordinated with mesh/object transport; compatible UV-only instances share resources.
- Alpha supports continuous blending and clipping, opaque culling is synchronized, image/group dependencies mark
  affected objects dirty, and explicit empty texture paths clear stale host bindings. Optional shader keywords
  preserve linear opacity and emission texture strength without changing existing material asset defaults.
- Surface-only non-Principled materials now export; volume-only helpers remain invisible and explicitly report
  unsupported volume transport. Shader lobe equivalence, ray/view-dependent effects and world lighting remain limits.
- Both Blender 5.0.1 smoke suites PASS. All 122 Hidden Alley mesh-slot materials export with zero exceptions and
  missing files; original datablock counts restored. Debug build PASS; ZIP's 13 Python modules match source.
- GPU run 04 PASS for controlled color changes, alpha cutout, transparent queue, continuous composition, texture
  unlink and restoration. 30 PNG frames / ten sequences, 1280x704, consecutive engine_frame per three-frame set.
  All nine backed-up files restored. Evidence:
  F:/AiluEngine/artifacts/out/editor/bin/x64/debug/BlenderMaterialBakeValidation-20261009-04/analysis.json.
- Full Hidden Alley live Editor appearance/performance remains open. First complex sync is more expensive because
  native bakes run on Blender's main thread. Details: completed/blender-material-graph-repair.md.

# Import Descriptor Page Growth Crash — 2026-10-09

- Fixed GPUVisibleDescriptorAllocator's staging-page pointer invalidation when its vector grew beyond ten pages.
  Allocations borrow page pointers; deque now preserves addresses. Removed the shared first-allocation reserve flag
  and ten-page assertion. Shader-visible heap capacities and fence/lifetime policy remain unchanged.
- Debug build PASS. Native DX12 regression PASS for two independent allocators: 16 pages/16,369 descriptors each,
  concurrent old-page copies, release/reuse/generation checks and zero D3D12 ERROR/CORRUPTION messages.
  All nine backed-up files restored. Evidence: Debug Editor directory/DescriptorPageGrowthValidation-20261009-02/.
- Full Hidden Alley import and visual acceptance remain open. Details: completed/descriptor-page-growth-crash.md.

# Blender Native Node Clone Crash — 2026-10-09

- Supplied Blender native/Python stack points to ChannelGraph.output while constructing RNA node input wrappers.
  Removed writes to shared node-type registration/bl_* properties; snapshot source inputs before recursive cloning.
  Source confirms unsafe shared-type writes; exact native crash has not been reproduced.
- Old code fails a native instance-write audit on bl_idname; repaired code passes and clones 1,488 real graphs.
  Interactive timer node-path test covers all 1,620 material/object combinations with restored datablock counts.
  Graph-only fixture does not bake textures or encode mesh payloads. Native Cycles smoke and 122 material
  representative exports PASS at 16px; zero missing paths/exceptions. Debug build PASS.
- ZIP rebuilt and installed/enabled in Blender 5.0; 13 files match, fresh startup hash verified. Full scene
  bake/mesh/Editor transport and visual acceptance remain open. Details: completed/blender-node-clone-crash.md.

# Blender Python Transport Thread Crash — 2026-10-09

- Second supplied crash is in network worker queue.get/PyException_SetTraceback while the main thread executes
  Blender timer/operators. Removed persistent Python worker; existing timer drives bounded nonblocking socket
  connect/handshake, fragmented reads, partial sends and retries. Packet generation advances sends during Full Sync.
- Real TCP regressions PASS for partial 8MB writes/SHA256, fragmented ACK, rejection, disconnect/reconnect,
  invalid packet, coalescing/deletion, overflow and refused port. Native timer Cycles regression while connected
  PASS for both source and installed module; four packets received, only MainThread, Thread.start forbidden.
- Debug build/package/install/startup PASS; 13 modules verified. Restart Blender to unload the old worker/module.
  Exact native crash determinism and full Hidden Alley import/visual/performance acceptance remain open.
  Details: completed/blender-threaded-transport-crash.md.

# Blender Full Sync Performance Regression — 2026-10-09

- Repaired per-object over-baking, retained channel/image buffers and scene-wide mesh cache. AUTO uses supported
  shared image materials/source UVs and explicit approximations; unresolved graphs use 256px native fallback.
  Explicit BAKE keeps native evaluation/unique atlas/1024px quality. Four mesh LRU entries and per-pass cleanup.
- Bulk typed mesh encoding and indexed mesh-loop vertices preserve corner attributes while avoiding Python tuple
  expansion and single-mesh 256MiB overflow. Missing/failed tangents use a deterministic default.
- Full Hidden Alley default-quality native UI timer + complete binary mesh encoding + real TCP peer PASS:
  3m26s, 1,363 meshes, 182 materials, 2,967 packets, 784,883,856 bytes; OS process peak working set 5.86GiB.
  Source datablock counts, scene and view layer restored. Earlier guarded/overflow runs are recorded as failures.
- Direct/native/indexed mesh/lifetime regressions, Debug build, package/install and installed TCP/native timer PASS.
  13 installed Python modules verified; restart Blender. Actual Editor import/VRAM/visual acceptance remains open.
  Details: completed/blender-sync-performance-regression.md; Saved/BlenderTextureValidation/performance-full-scene-05.json.

# Blender Export Self-Generated Updates — 2026-10-10

- Supplied FullSync summary ends one batch; later material packets are new incremental messages. Reproduced
  exporter-induced feedback: native procedural plane sent nine idle object/material/mesh updates in 1.5 seconds.
- Full/incremental exports suppress exporter side effects and drain deferred source view-layer notifications
  before restoring tracking. Real pending edits are processed before entry; no time window drops user edits.
- Source/installed native timer regressions PASS: no idle resend, transform/material edits each send one update,
  no subsequent self-trigger; exception cleanup restores tracking. Native dependency/channel regression PASS.
- Full Hidden Alley default-quality native export/idle PASS: 199.125s / 2,967 full messages / zero idle updates;
  a transform edit still sends exactly one object/material/mesh update. Recording peer, not live Editor acceptance.
- Debug build/package/install PASS; 13 installed files verified, backup 20261010-003212. Restart Blender.
  Details: completed/blender-sync-self-generated-updates.md; Saved/BlenderTextureValidation/sync-idle-full-scene.json.
