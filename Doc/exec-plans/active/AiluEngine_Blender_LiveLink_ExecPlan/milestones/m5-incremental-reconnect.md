# M5 — Incremental Sync, Coalescing, Reconnect

# Purpose

在 Full Sync 正确性的基础上，把 Live Link 变成真正可持续编辑的低延迟增量系统。

---

# Scope

实现：

- depsgraph dirty collector。
- transform dirty。
- geometry dirty。
- shading dirty。
- delete detection。
- dirty debounce。
- Ailu command coalescing。
- queue backpressure。
- reconnect。
- auto full resync。
- stale object cleanup。
- session clear。
- scene switch recovery。
- play-mode recovery。

---

# Non-goals

- distributed collaboration。
- conflict resolution。
- bidirectional merge。
- undo history transport。
- guaranteed delivery across process restart。

---

# Architecture Constraints

## Full Sync authoritative

Incremental 不是唯一真相。

任何：

```text
reconnect
scene switch
protocol desync
queue overflow
```

都允许：

```text
discard incremental state
request full sync
```

## Latest Wins

Transform/Material/Mesh 更新按 GUID + revision coalesce。

## Bounded Queue

禁止 network producer 无限增长内存。

---

# Baseline

M1 已有 Full Sync。

M2/M3/M4 已有 resource-specific upsert。

---

# Target Outcome

连续在 Blender 中：

```text
拖 gizmo
改 modifier
改 material
删 object
duplicate object
改 parent
```

Ailu 保持稳定，不积压几秒钟旧更新。

断开网络数秒再连：

```text
Full Sync
```

恢复一致。

---

# Relevant Files

```text
Plugins/BlenderLiveLink/Inc/LiveLinkCommandQueue.h
Plugins/BlenderLiveLink/Inc/LiveLinkSession.h
Plugins/BlenderLiveLink/Src/LiveLinkSession.cpp
Plugins/BlenderLiveLink/Src/BlenderLiveLinkPlugin.cpp

Tools/Blender/ailu_live_link/dirty_tracker.py
Tools/Blender/ailu_live_link/connection.py
Tools/Blender/ailu_live_link/object_sync.py
```

---

# Milestones

当前：

```text
M5
```

---

# Current Milestone

M5。

---

# Implementation

## 5.1 Dirty Handler

使用：

```python
bpy.app.handlers.depsgraph_update_post
```

handler：

```text
inspect update.id
mark dirty
return
```

不要在 handler 内序列化 100k triangle mesh。

---

## 5.2 Dirty Classes

最少：

```text
transform_dirty
geometry_dirty
material_dirty
scene_structure_dirty
```

如果 geometry dirty：

```text
也可能隐含 material slot/transform
```

但不要无脑 full sync。

---

## 5.3 Timer Flush

`bpy.app.timers`：

```text
~16-50 ms
```

flush：

```text
collect dirty snapshot
build payloads on main thread
enqueue immutable bytes
```

---

## 5.4 Deletion Detection

Depsgraph 对 deleted ID 不一定能作为唯一稳定来源。

维护：

```text
previous_synced_object_guids
current_scene_object_guids
```

周期比较：

```text
removed = previous - current
```

发送 delete。

Full Sync 仍兜底。

---

## 5.5 Ailu Queue

建议 command metadata：

```text
type
guid
revision
payload
```

coalesce key：

```text
(type class, guid)
```

例如：

```text
transform -> latest only
material  -> latest only
mesh      -> latest only if not applying
```

Delete：

```text
优先级高于之前 queued upsert
```

---

## 5.6 Queue Bounds

至少：

```text
max command count
max queued bytes
```

超限：

```text
drop/coalesce
mark needs full resync
```

不要继续接受无限 Mesh 包。

---

## 5.7 Reconnect

Blender socket worker：

```text
Disconnected
    ↓
retry with backoff
    ↓
Connected
    ↓
HELLO
    ↓
Full Sync
```

可以先让 UI 控制 auto reconnect。

---

## 5.8 Scene Switch

Blender scene change：

```text
new session/full sync
```

Ailu active scene change：

```text
clear old transient root mapping
needs full sync
```

---

## 5.9 Play Mode Return

进入 Play：

```text
suspend apply
```

回 Edit：

```text
clear session
full sync
```

不要 replay Play 期间累计的所有 incremental command。

---

## 5.10 Feedback

Blender UI 至少显示：

```text
Disconnected
Connecting
Connected
Syncing
Error
```

以及：

```text
last sync time
queued bytes
last error
```

Ailu Editor UI 非本 milestone 必须项。

---

# Progress

- [x] depsgraph handler。
- [x] dirty classification。
- [x] timer flush。
- [x] delete detection。
- [x] transform debounce。
- [x] mesh debounce。
- [x] material debounce。
- [x] Ailu command coalesce。
- [x] delete precedence。
- [x] queue byte bound。
- [x] overflow recovery source path。
- [x] reconnect。
- [x] auto full sync。
- [x] scene switch recovery。
- [x] play mode recovery。
- [x] status UI。

源码与 Editor Debug 构建通过；连续编辑、断连恢复和 overflow recovery 压力验收未运行。

---

# Decision Log

- Full Sync authoritative。
- latest wins。
- bounded queue。
- overflow -> resync。
- no replay of Play Mode backlog。

---

# Surprises & Discoveries

记录 Blender depsgraph 实际事件粒度。

特别关注：

```text
仅 transform 是否意外报告 geometry dirty
material parameter update 对哪些 ID 报 update
delete 是否触发稳定 update
```

根据实测优化，不依据理论猜测。

---

# Validation

连续编辑测试 10 分钟：

```text
move
rotate
scale
duplicate
delete
reparent
edit mesh
modifier
material
light
camera
```

断网测试：

```text
kill Ailu
restart
reconnect
```

以及：

```text
kill Blender socket
plugin unload/reload
```

最后 Full Sync 后比较：

```text
Blender object GUID set
Ailu LiveLink entity GUID set
```

必须一致。

---

# Known Follow-ups

- delta mesh。
- transport compression。
- priority scheduling。
- adaptive debounce。

---

# Outcomes

M5 完成后系统从“能同步”进入“能长期使用”。

---

# Resume Protocol

遇到 desync 时优先证明 Full Sync 是否正确。

不要用更多 incremental patch 掩盖 Full Sync bug。
