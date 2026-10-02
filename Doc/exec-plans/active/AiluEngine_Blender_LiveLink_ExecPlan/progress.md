# AiluEngine Blender Live Link — Progress

> Status: Active
>
> Current Milestone: M0 — Plugin Scaffold + Loopback Transport
>
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

Status:

```text
CURRENT
```

File:

```text
milestones/m0-plugin-transport.md
```

Progress:

- [ ] AiluBlenderLiveLink target。
- [ ] plugin.json。
- [ ] autoload。
- [ ] loopback TCP。
- [ ] worker lifecycle。
- [ ] protocol header。
- [ ] HELLO。
- [ ] PING。
- [ ] malformed guard。
- [ ] active-client unload test。

---

## M1 — Session Identity + Transient Scene + Transform

Status:

```text
NOT STARTED
```

File:

```text
milestones/m1-session-transform.md
```

Progress:

- [ ] generic transient entity semantics。
- [ ] Blender addon skeleton。
- [ ] persistent UUID。
- [ ] session root。
- [ ] Full Sync。
- [ ] create/delete。
- [ ] hierarchy。
- [ ] transform。
- [ ] coordinate golden tests。
- [ ] save exclusion。

---

## M2 — Static Mesh

Status:

```text
NOT STARTED
```

File:

```text
milestones/m2-static-mesh.md
```

Progress:

- [ ] evaluated mesh。
- [ ] binary payload。
- [ ] runtime Mesh。
- [ ] normals/tangents/UV。
- [ ] submesh。
- [ ] bounds。
- [ ] debounce。
- [ ] large mesh validation。

---

## M3 — Material + Texture

Status:

```text
NOT STARTED
```

File:

```text
milestones/m3-material-texture.md
```

Progress:

- [ ] Principled material。
- [ ] scalar PBR。
- [ ] BaseColor texture。
- [ ] Normal texture。
- [ ] RM packing。
- [ ] packed/generated image。
- [ ] material slots。
- [ ] reload/cache。

---

## M4 — Light + Camera

Status:

```text
NOT STARTED
```

File:

```text
milestones/m4-light-camera.md
```

Progress:

- [ ] Sun。
- [ ] Point。
- [ ] Spot。
- [ ] Rect Area。
- [ ] intensity mapping。
- [ ] Perspective。
- [ ] Orthographic。
- [ ] semantic orientation validation。

---

## M5 — Incremental + Reconnect

Status:

```text
NOT STARTED
```

File:

```text
milestones/m5-incremental-reconnect.md
```

Progress:

- [ ] depsgraph dirty collector。
- [ ] debounce。
- [ ] coalescing。
- [ ] deletion。
- [ ] bounded queue。
- [ ] reconnect。
- [ ] full resync。
- [ ] play mode recovery。

---

## M6 — Stability + Package

Status:

```text
NOT STARTED
```

File:

```text
milestones/m6-stability-package.md
```

Progress:

- [ ] stress。
- [ ] metrics。
- [ ] memory cleanup。
- [ ] Blender addon zip。
- [ ] Ailu plugin deploy。
- [ ] final acceptance。

---

# Current Milestone

```text
M0 — Plugin Scaffold + Loopback Transport
```

当前 Next Actions：

1. 在实际仓库中确认插件 target 的 CMake 接入习惯。
2. 创建 `Plugins/BlenderLiveLink` target。
3. 创建 `plugin.json`。
4. 只实现空插件 Load/Tick/Unload。
5. 加入 `LiveLinkServer`。
6. loopback bind。
7. Python smoke client HELLO/PING。
8. active client 下执行 plugin unload。
9. 把实际测试结果写回本文件。

在 M0 完成前：

```text
不要实现 Scene Entity
不要实现 Mesh
不要实现 Blender depsgraph
```

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
- [ ] 开始 M0 代码实现。

---

# Latest Validation

当前仅完成：

```text
source-level architecture inspection
```

尚未执行：

```text
Live Link build
Live Link runtime test
Blender addon test
```

所以不得把任何 milestone 标记为 PASS。

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
Planning complete
Implementation not started
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
