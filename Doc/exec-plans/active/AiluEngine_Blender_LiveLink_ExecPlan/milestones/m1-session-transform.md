# M1 — Session Identity + Transient Scene + Transform / Hierarchy

# Purpose

建立 Blender Object 与 Ailu Scene Entity 的稳定一一对应，并解决 Live Link entity 不应进入 Scene Save 的问题。

---

# Scope

实现：

- Blender Add-on 基础包。
- Blender object persistent UUID。
- Live Link session GUID。
- Full Sync Begin/End。
- transient scene root。
- object create/update/delete。
- name。
- parent hierarchy。
- local transform。
- coordinate conversion module。
- reconnect full-sync reconciliation。
- play-mode / temporary-scene suspend。

---

# Non-goals

- Mesh。
- Material。
- Texture。
- Light。
- Camera。
- Collection。
- Prefab。
- runtime scene sync。

---

# Architecture Constraints

## Persistent Identity

禁止名字作为 key。

```text
Blender Object UUID -> Ailu Entity Guid
```

## Transient

Live Link entity 不允许保存到 Scene Asset。

## Hierarchy

先 create 全部 object，再建立 parent。

不要要求父对象一定先到。

## Transform

统一使用 coordinate conversion module。

不要在 object/light/camera exporter 中各写一套 swizzle。

---

# Baseline

Ailu 已有：

```text
PersistentIdComponent
Scene::AddObject(name, requested_guid, ...)
Scene::FindEntity(Guid)
Scene::Reparent
Scene::RemoveObject
TransformComponent setters
```

缺失：

```text
generic transient entity save exclusion
```

---

# Target Outcome

Blender：

```text
EmptyRoot
├── Cube
└── Empty
    └── Suzanne
```

Ailu：

```text
__BlenderLiveLink_<session>
├── EmptyRoot
│   ├── Cube
│   └── Empty
│       └── Suzanne
```

保存 Ailu Scene 后重新打开：

```text
Live Link subtree 不存在
```

重新 Connect + Full Sync：

```text
重新构造
```

---

# Relevant Files

Ailu：

```text
Engine/Inc/Scene/Component.h
Engine/Inc/Scene/Scene.h
Engine/Src/Scene/Scene.cpp
Engine/Src/Scene/EntitySerializer.cpp
Engine/Src/Assets/AssetHandlers.cpp

Plugins/BlenderLiveLink/...
```

Blender：

```text
Tools/Blender/ailu_live_link/__init__.py
Tools/Blender/ailu_live_link/addon.py
Tools/Blender/ailu_live_link/connection.py
Tools/Blender/ailu_live_link/protocol.py
Tools/Blender/ailu_live_link/dirty_tracker.py
Tools/Blender/ailu_live_link/coordinate.py
Tools/Blender/ailu_live_link/object_sync.py
Tools/Blender/ailu_live_link/ui.py
```

---

# Milestones

当前：

```text
M1
```

依赖：

```text
M0 PASS
```

下一阶段：

```text
M2 Static Mesh
```

---

# Current Milestone

M1。

---

# Implementation

## 1.1 Generic Transient Entity Flag

推荐最小修改：

```cpp
struct TagComponent
{
    ...
    bool _is_transient = false;
};
```

要求：

- runtime-only。
- 不写入 Scene document。
- Scene save entity traversal 遇到 transient 直接 skip。
- Scene root 不受影响。

如果工程里已有更通用的 entity flags 机制，应复用该机制，不重复添加 bool。

---

## 1.2 Live Link Root

建立：

```text
__BlenderLiveLink_<short_session_guid>
```

root：

```text
transient = true
```

所有同步 object：

```text
transient = true
parent under session root unless Blender has parent
```

---

## 1.3 UUID

Blender helper：

```python
def GetOrCreateGuid(data_block):
    ...
```

属性名固定，例如：

```text
ailu_live_link_guid
```

Scene：

```text
ailu_live_link_session_guid
```

检测 duplicate GUID：

```text
Full Sync 前扫描所有参与同步 Object
    ↓
同一 GUID 出现两次
    ↓
保留第一个
    ↓
后续对象生成新 GUID
```

因为 Blender duplication 可能复制 custom properties。

---

## 1.4 Object Upsert JSON

建议至少：

```json
{
    "guid": "...",
    "name": "Cube",
    "parent_guid": "...",
    "position": [0, 0, 0],
    "rotation": [0, 0, 0, 1],
    "scale": [1, 1, 1],
    "visible": true
}
```

rotation 使用 quaternion。

不要发 Euler。

---

## 1.5 Full Sync Apply

```text
FULL_SYNC_BEGIN
    ↓
create/find session root
mark existing mappings unseen

OBJECT_UPSERT pass 1
    ↓
create/update all entities

OBJECT_UPSERT pass 2 / deferred parent list
    ↓
Reparent

FULL_SYNC_END
    ↓
remove unseen stale entities
```

要允许 Blender 顺序任意。

---

## 1.6 Coordinate Module

Blender side集中：

```text
BlenderToAiluPosition
BlenderToAiluDirection
BlenderToAiluScale
BlenderToAiluMatrix
BlenderToAiluQuaternion
```

推荐先以 matrix conversion 建立 golden values，再从 matrix 提取 TRS。

不要凭感觉交换 Euler 分量。

---

## 1.7 Scene Switch

Ailu ActiveScene 变化时：

```text
旧 entity mapping 全部失效
```

MVP：

```text
detect scene pointer/guid changed in Tick
    ↓
clear session state
    ↓
send/request resync-needed status
```

最简单可以让 Blender 端每次收到 reconnect / error 后用户 Full Sync。

M5 再自动化。

---

## 1.8 Play Mode

如果：

```cpp
Application::Get()._is_playing_mode
```

则：

```text
不 Apply Scene command
设置 _needs_full_resync = true
```

回 Edit Mode：

```text
ClearSession
等待 Full Sync
```

---

# Progress

- [x] transient entity generic support。
- [x] Scene save skip transient。
- [ ] Blender addon enable/disable。
- [x] Connect/Disconnect panel。
- [x] object UUID。
- [x] session UUID。
- [x] duplicate UUID repair。
- [x] full sync begin/end。
- [x] create。
- [x] delete。
- [x] rename。
- [x] hierarchy。
- [x] transform。
- [ ] coordinate golden tests。
- [x] reconnect full sync source path。
- [x] play mode suspend source path。
- [x] temporary scene suspend source path。

源码与 Editor Debug 构建通过；Blender add-on 注册、live scene sync 和坐标 golden tests 未运行。

---

# Decision Log

- Object GUID 作为 Entity PersistentId。
- Live Link subtree transient。
- transform quaternion。
- hierarchy 两阶段 apply。
- coordinate conversion 独立模块。

---

# Surprises & Discoveries

重点记录：

- Scene save 的实际 entity traversal 是否有其他入口。
- Prefab serialization 是否也需要 transient guard。
- negative scale 对 local transform decomposition 的影响。
- Blender parent inverse matrix 是否必须 bake 到 local matrix。

如果 parent inverse 引起错误，应优先同步 Blender `matrix_local` / evaluated transform semantics，而不是继续修 Euler。

---

# Validation

建立 `live_link_transform_test.blend`：

```text
Root
├── Identity
├── PosX
├── PosY
├── PosZ
├── RotX90
├── RotY90
├── RotZ90
├── Scale
├── NegativeScale
└── Parent
    └── Child
```

验证：

1. Full Sync hierarchy 一致。
2. 世界位置一致。
3. 朝向一致。
4. parent move 后 child 正确。
5. rename 不改变 GUID。
6. duplicate 得到新 GUID。
7. delete 清理。
8. Save Scene。
9. 重开 Scene。
10. Live Link entity 不存在。
11. reconnect + Full Sync 恢复。
12. Play Mode 期间 Blender 修改不写 Runtime Scene。

---

# Known Follow-ups

- hidden collection policy。
- collection mapping。
- selection sync。
- prefab conversion。

---

# Outcomes

M1 完成后，后续所有 resource sync 都有稳定 object identity 与 session ownership。

---

# Resume Protocol

只在 M0 完成后执行。

若坐标系验证未完全通过，不进入 M2。
