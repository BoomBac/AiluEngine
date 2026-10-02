# M6 — Stability, Performance, Packaging, Final Acceptance

# Purpose

把已经功能完整的 Live Link 做成可以日常开发使用的工具，并完成最终性能/稳定性验收。

---

# Scope

- stress。
- reconnect stress。
- plugin unload stress。
- large scene。
- large mesh。
- log quality。
- error handling。
- Blender Add-on install package。
- Ailu plugin deploy layout。
- cache cleanup。
- README/usage。
- final validation report。

---

# Non-goals

本阶段不新增大功能。

发现新需求记录到 Known Follow-ups。

除非阻塞稳定性，不扩展同步类型。

---

# Architecture Constraints

## 不以优化破坏正确性

M6 可以优化：

```text
allocation
serialization
copy
debounce
coalescing
```

但不能改变 wire semantics 而不 bump protocol version。

## 测量后优化

所有性能修改先有 baseline。

---

# Baseline

M0~M5 均 PASS。

---

# Target Outcome

用户可以：

1. 安装 Blender Add-on。
2. 启动 Ailu Editor。
3. Connect。
4. Full Sync。
5. 正常编辑 30 分钟。
6. Disconnect。
7. 再 Connect。
8. 关闭 Blender/Ailu 任一端。
9. 不 crash。
10. 不污染 Scene Asset。

---

# Relevant Files

整个：

```text
Plugins/BlenderLiveLink/
Tools/Blender/ailu_live_link/
```

以及：

```text
README / install scripts / CMake staging
```

---

# Milestones

当前：

```text
M6
```

最后里程碑。

---

# Current Milestone

M6。

---

# Implementation

## 6.1 Metrics

至少记录：

```text
transform one-way / end-to-end latency
100k triangle mesh:
    Blender extraction
    encode
    send
    decode
    Ailu Mesh::Apply
    total

1000 object full sync
material update
texture update
```

不要求建立复杂 profiler UI。

日志或 test output 即可。

---

## 6.2 Stress Matrix

### Connection

```text
connect/disconnect x100
```

### Plugin

client connected 时：

```text
plugin unload/reload x50
```

### Object

```text
1000 object full sync
5000 transform-only objects if practical
```

### Mesh

```text
1k
100k
500k triangles
```

至少记录实际可接受上限。

### Edit

快速：

```text
gizmo drag
modifier slider
material slider
```

确认 queue 不持续增长。

---

## 6.3 Memory

观察：

```text
connect
full sync
clear session
disconnect
```

后：

```text
transient entity count
Mesh refs
Material refs
Texture refs
queued bytes
```

应回落。

可以允许 ResourceMgr 内部共享 cache，但不能由 Live Link 自己无限保留旧 revision。

---

## 6.4 Temp Texture Cache

定义清理策略：

```text
current session owns files
```

disconnect：

- 可延迟删除。
- 下次 addon 启动清理 stale session directory。

不要在 image 正被 Ailu 读取时抢删。

---

## 6.5 Error Messages

Blender UI：

```text
Cannot connect
Protocol mismatch
Ailu closed connection
Texture missing
Unsupported material
Payload rejected
```

Ailu log 必须包含：

```text
session
message type
guid when available
reason
```

不要每帧 spam。

---

## 6.6 Blender Packaging

产物：

```text
ailu_live_link.zip
```

用户通过 Blender：

```text
Edit
Preferences
Add-ons
Install from Disk
```

安装。

Addon version 与 protocol version 分开。

---

## 6.7 Ailu Plugin Packaging

最终：

```text
Plugins/BlenderLiveLink/
├── plugin.json
└── AiluBlenderLiveLink.dll
```

需要的 runtime DLL 如果有，也放 plugin folder 并明确 staging。

尽量只依赖：

```text
Engine.dll
Windows sockets
C++ runtime
```

---

## 6.8 Usage Doc

最少：

```text
Requirements
Install
Enable
Connect
Full Sync
Auto Sync
Supported Data
Unsupported Data
Troubleshooting
```

---

# Progress

- [ ] metrics。
- [ ] connect x100。
- [ ] unload x50 active client。
- [ ] 1000 object。
- [ ] 100k mesh。
- [ ] 500k mesh。
- [ ] queue stability。
- [ ] memory cleanup。
- [ ] temp cache cleanup。
- [ ] error UX。
- [ ] Blender zip。
- [ ] Ailu plugin staging。
- [ ] user README。
- [ ] final validation report。

---

# Decision Log

M6 不新增同步类别。

---

# Surprises & Discoveries

记录实际瓶颈：

```text
Blender evaluated mesh extraction
Python pack
socket copy
Ailu decode
Mesh::Apply
GPU upload
texture import
```

然后只优化最高占比。

---

# Validation

最终 acceptance：

## Lifecycle

- [ ] Ailu Editor start。
- [ ] plugin autoload。
- [ ] Blender connect。
- [ ] Blender close while connected。
- [ ] reconnect。
- [ ] Ailu plugin unload while connected。
- [ ] reload。
- [ ] Editor shutdown while connected。
- [ ] zero crash/hang。

## Scene

- [ ] create。
- [ ] delete。
- [ ] rename。
- [ ] reparent。
- [ ] transform。
- [ ] duplicate UUID repair。
- [ ] Save Scene no transient entity。

## Mesh

- [ ] modifier。
- [ ] UV seam。
- [ ] normals。
- [ ] tangents。
- [ ] multi material。
- [ ] large mesh。

## Material

- [ ] BaseColor。
- [ ] R/M。
- [ ] Normal。
- [ ] Emission。
- [ ] Texture reload。

## Light

- [ ] sun。
- [ ] point。
- [ ] spot。
- [ ] rectangle area。

## Camera

- [ ] perspective FOV。
- [ ] orientation。
- [ ] ortho。

## Recovery

- [ ] reconnect full sync。
- [ ] stale delete。
- [ ] queue overflow recovery。
- [ ] Play Mode return full sync。

---

# Known Follow-ups

最终把未做项目移动到根 `progress.md`。

---

# Outcomes

M6 完成即宣布：

```text
AiluEngine Blender Live Link MVP Complete
```

---

# Resume Protocol

如果当前只剩性能问题：

1. 先读取已有 metric。
2. 只优化最大热点。
3. 每次优化前后记录数字。
4. 不通过删 validation 来“完成”MVP。
