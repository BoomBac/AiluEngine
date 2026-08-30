# AiluEngine Animation Clip Timeline & Preview 开发任务

## 目标

为 `AnimationClipEditor` 增加骨骼动画预览和时间轴。

本次不要直接实现复杂 Animation Window / Sequencer，而是先完成：

1. 通用轻量 `TimelineView`
2. 骨骼动画 `AnimationTimeline`
3. `AnimationClipPreview`
4. 将三者接入现有 `AnimationClipEditor`

最终要求：

- 可选择一个 `SkeletonMesh` 作为预览模型
- 可播放 / 暂停 / 停止骨骼动画
- 拖动 Timeline Playhead 时实时预览对应 Pose
- Timeline 显示骨骼 Track、采样帧和 Animation Event
- 尽量复用现有动画、蒙皮和 Editor UI 设施

---

# 1. Timeline 通用层

新增建议目录：

```text
Editor/UI/Timeline/
    TimelineView.h
    TimelineView.cpp
```

`TimelineView` 只处理“时间轴通用能力”，禁止依赖：

```text
AnimationClip
Skeleton
Joint
AnimationEvent
```

## 基础能力

实现：

```text
time range
current time / playhead
ruler
horizontal zoom
horizontal scroll
vertical scroll
track layout
time <-> pixel conversion
mouse scrub
snap
visible time range
```

建议核心接口：

```cpp
class TimelineView
{
public:
    void SetTimeRange(f32 start_time, f32 end_time);
    void SetCurrentTime(f32 time);

    f32 CurrentTime() const;

    void SetPixelsPerSecond(f32 pixels_per_second);
    void SetSnapInterval(f32 interval);

    f32 TimeToLocalX(f32 time) const;
    f32 LocalXToTime(f32 x) const;

    f32 VisibleStartTime() const;
    f32 VisibleEndTime() const;

    DECLARE_DELEGATE(on_time_changed, f32);
};
```

内部状态类似：

```cpp
f32 _start_time = 0.0f;
f32 _end_time = 1.0f;
f32 _current_time = 0.0f;

f32 _pixels_per_second = 100.0f;

f32 _scroll_x = 0.0f;
f32 _scroll_y = 0.0f;

f32 _header_width = 160.0f;
f32 _ruler_height = 24.0f;
```

## Zoom

鼠标滚轮缩放时：

**保持鼠标所在位置对应的时间不变。**

不要简单修改 `_pixels_per_second` 导致 Timeline 跳动。

## Snap

提供：

```cpp
SetSnapInterval(1.0f / frame_rate);
```

拖动 playhead 时按采样帧吸附。

## 性能要求

Timeline 中的：

```text
track
key
frame marker
event marker
```

不要分别创建大量 `UIElement`。

应由一个 Timeline Widget 在 `RenderImpl()` 中直接绘制。

---

# 2. AnimationTimeline

新增：

```text
Editor/Animation/
    AnimationTimeline.h
    AnimationTimeline.cpp
```

它是 `TimelineView` 的 Animation Clip 使用层。

职责：

```text
显示骨骼名称
显示骨骼 Track
显示每个 Track 的采样 Frame
显示 Animation Event
绘制 Playhead
处理骨骼选择
处理 Timeline Scrub
```

禁止把这些 Animation 专用概念下沉进 `TimelineView`。

---

# 3. 当前 AnimationClip 的数据模型

当前骨骼 Clip 是固定帧率均匀采样：

```text
frame 0 -> 0 / frame_rate
frame 1 -> 1 / frame_rate
frame 2 -> 2 / frame_rate
...
```

`AnimationClipFrameDocument` 当前没有独立 `_time`。

因此本阶段 Timeline 定义为：

> Imported sampled animation timeline

第一版不要实现：

```text
任意时间 Key
拖动 Key
删除 / 插入 Key
Bezier Curve
Curve Editor
Position / Rotation / Scale 独立 Channel
```

AnimationTimeline 直接按照：

```cpp
time = frame_index / frame_rate;
```

绘制采样 Frame 即可。

---

# 4. AnimationClipPreview

新增：

```text
Editor/Animation/
    AnimationClipPreview.h
    AnimationClipPreview.cpp
```

职责：

```text
管理预览 SkeletonMesh
动画 Pose Evaluate
Skinning
预览 Camera
预览 Light
RenderTexture
Animation Preview Render
```

建议结构：

```cpp
class AnimationClipPreview
{
public:
    void SetClip(AnimationClip *clip);
    void SetMesh(Render::SkeletonMesh *mesh);

    void SetTime(f32 time);

    Render::RenderTexture *GetRenderTexture() const;

private:
    void RebuildPreviewMesh();
    void EvaluatePose();
    void UpdateSkinning();
    void RenderPreview();

private:
    AnimationClip *_clip = nullptr;
    Render::SkeletonMesh *_source_mesh = nullptr;

    Ref<Render::SkeletonMesh> _preview_mesh;

    SkeletonAnimationBinding _binding;
    SkeletonPose _pose;

    Vector<Matrix4x4f> _palette;
};
```

---

# 5. 必须复用 Runtime 动画采样路径

Editor 禁止重新实现一套 Animation Clip Evaluate。

优先复用：

```text
AnimationClip
SkeletonAnimationBinding
AnimationEvaluation
SkeletonPose
SkinningSystem
```

流程：

```text
AnimationClip
      ↓
SkeletonAnimationBinding
      ↓
SkeletonPose
      ↓
Matrix Palette
      ↓
SkinningSystem
      ↓
Preview SkeletonMesh
```

保证：

> Editor Preview 和 Runtime 使用同一套动画采样逻辑。

---

# 6. Preview Mesh 必须复制

当前 `SkinningSystem` 会直接修改 Mesh VertexBuffer。

因此：

**禁止直接对 Asset Resource 中的 SkeletonMesh 执行 Preview Skinning。**

否则 Scene 中正在使用相同 SkeletonMesh 的对象也可能被修改。

需要为 Preview 创建独立 Mesh 实例：

```text
source SkeletonMesh
        ↓
Clone
        ↓
preview SkeletonMesh
        ↓
Skinning
```

复制：

```text
vertices
normals
tangents
colors
uv
indices
bone weights
bone indices
skeleton
mesh bind transform
```

然后重新 `Apply()`。

可以新增：

```text
Editor/Render/PreviewMeshUtils
```

用于 Editor Preview Mesh clone。

不要为了 Editor Preview 修改 Runtime `SkeletonMesh` 职责。

---

# 7. Preview Renderer

参考现有：

```text
AssetPreviewGenerator
```

复用其：

```text
Camera
Directional Light
RenderTexture
DepthTexture
CommandBuffer
DrawMesh
```

但不要直接每帧调用静态 Snapshot 接口。

动画 Preview 要：

```text
持续持有 RenderTexture
持续持有 PreviewMesh
每帧只更新 Pose / Skinning / CommandBuffer
```

尽量避免：

```cpp
ExecuteCommandBufferSync()
```

造成播放动画时 CPU/GPU 强制同步。

---

# 8. Preview Mesh 选择

在 `AnimationClipEditor` 增加：

```text
Preview Mesh [SkeletonMesh ▼]
```

复用现有 Object Asset Dropdown。

第一版允许用户手动选择。

可以在：

```cpp
AnimationClipAssetDocument
```

中增加：

```cpp
Guid _preview_mesh_guid = Guid::EmptyGuid();
```

作为 Editor Preview Metadata。

后续再考虑 FBX 导入时自动建立：

```text
AnimationClip -> SkeletonMesh
```

关系。

---

# 9. AnimationClipEditor 重构

避免继续把全部逻辑堆进现有 `AnimationClipEditor`。

目标结构：

```text
AnimationClipEditor
│
├── AnimationClipPreview
│
├── AnimationTimeline
│
└── Property / Toolbar
```

`AnimationClipEditor` 只负责协调：

```text
clip
preview mesh
preview time
playback state
timeline
preview renderer
```

---

# 10. 时间同步

统一使用 `_preview_time` 作为时间源。

播放：

```text
AnimationClipEditor::Update(dt)
        ↓
_preview_time += dt
        ↓
AnimationTimeline::SetCurrentTime()
        ↓
AnimationClipPreview::SetTime()
```

Timeline Scrub：

```text
mouse drag
    ↓
AnimationTimeline
    ↓
on_time_changed
    ↓
AnimationClipEditor::_preview_time
    ↓
AnimationClipPreview::SetTime()
```

不要让 Preview 和 Timeline 各维护一套独立时间。

---

# 11. 第一版 Timeline UI

目标效果：

```text
-----------------------------------------------------------
| ▶ ■ | 0.83 / 2.0s | Loop | 30 FPS                       |
-----------------------------------------------------------
| Preview                     | Clip Properties            |
|                             | Preview Mesh               |
|       Skeleton Mesh         | Duration                   |
|                             | Frame Rate                 |
-----------------------------------------------------------
| Events       ◆                         ◆                 |
| Root         ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆                |
| Spine        ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆                |
| Head         ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆                |
| Tail         ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆ ◆                |
|              0       10      20      30      40          |
|                              ▲                            |
-----------------------------------------------------------
```

第一版重点：

```text
正确
稳定
可 Scrub
可播放
性能合理
```

暂时不追求复杂编辑能力。

---

# 12. 推荐实现顺序

## Phase 1

实现 `TimelineView`：

```text
ruler
playhead
time/pixel conversion
zoom
scroll
snap
scrub
```

完成后马上进入真实 Animation 场景，不继续扩展抽象。

## Phase 2

实现 `AnimationClipPreview`：

```text
Preview Mesh clone
Animation Evaluate
SkeletonPose
Skinning
RenderTexture
```

确认：

```text
Clip + SkeletonMesh + Time
```

可以产生正确动画画面。

## Phase 3

实现 `AnimationTimeline`：

```text
bone rows
frame markers
event markers
playhead
bone selection
```

## Phase 4

接入 `AnimationClipEditor`：

```text
Play
Pause
Stop
Loop
Timeline Scrub
Preview Mesh
```

## Phase 5

最后再补：

```text
Orbit Camera
Grid
Show Skeleton
Previous / Next Frame
Playback Speed
Auto preview mesh
```

---

# 非目标

本任务暂时不要实现：

```text
Animator Controller 编辑
State Machine
Sequencer
Animation Blend
Curve Editor
IK
Retarget
Animation Compression
复杂 Keyframe Editing
完整 Unity Animation Window
```

---

# 核心原则

1. `TimelineView` 只理解“时间”，不理解 Animation。
2. Timeline 元素直接绘制，避免一个 Key 一个 UIElement。
3. Runtime 与 Editor Preview 共用 Animation Evaluation。
4. Preview Skinning 必须使用独立 Mesh 副本。
5. 先做 Preview + Scrub，再扩展编辑能力。
6. 不提前设计完整 Sequencer Framework。
7. Timeline 的通用能力必须由 AnimationClipEditor 的实际需求驱动。