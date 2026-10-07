# M4 — Light + Camera Sync

# Purpose

同步 Blender 场景中的基础灯光和相机，并通过 semantic direction 解决两个系统 local axis 不一致的问题。

---

# Scope

Light：

- SUN。
- POINT。
- SPOT。
- AREA Square/Rectangle。
- color。
- intensity scale。
- range。
- radius。
- spot inner/outer angle。
- area width/height。
- transform/orientation。

Camera：

- Perspective。
- Orthographic。
- position。
- forward/up。
- horizontal FOV。
- near/far。
- aspect。
- ortho size。

---

# Non-goals

- IES。
- Blender Cycles-specific light behavior。
- exact photometric equivalence。
- Area Disc/Ellipse exact integration。
- depth of field。
- aperture/bokeh。
- camera shift。
- panoramic/fisheye。
- exposure matching。

---

# Architecture Constraints

## 不发送 raw Euler

Light / Camera 发送 semantic vectors。

Camera：

```text
position
forward
up
```

Directional / Spot：

```text
direction
```

## Unit Conversion

明确写 conversion，不隐藏 magic。

## Intensity

提供 `light_intensity_scale`。

---

# Baseline

Ailu：

```text
ELightType
LightComponent
LightingSystem
CCamera
Render::Camera
Quaternion::LookRotation
```

Ailu Camera `FovH` 为 degree。

---

# Target Outcome

Blender 相机视图与 Ailu camera framing 基本一致。

Blender Sun/Spot 指向目标时，Ailu 光方向一致。

---

# Relevant Files

```text
Engine/Inc/Scene/Component.h
Engine/Src/Scene/RenderSystem.cpp
Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp

Plugins/BlenderLiveLink/Inc/LiveLinkSceneBridge.h
Plugins/BlenderLiveLink/Src/LiveLinkSceneBridge.cpp

Tools/Blender/ailu_live_link/light_sync.py
Tools/Blender/ailu_live_link/camera_sync.py
Tools/Blender/ailu_live_link/coordinate.py
```

---

# Milestones

当前：

```text
M4
```

---

# Current Milestone

M4。

---

# Implementation

## 4.1 Light Entity

沿用 M1 object entity。

对象是 light：

```text
ensure LightComponent
```

类型变回 mesh/empty：

```text
remove/disable old LightComponent
```

类型 mutation 需要稳定处理。

---

## 4.2 SUN

Blender world direction：

```text
local -Z transformed to world
```

转 Ailu direction。

Ailu rotation：

```cpp
Quaternion::FromTo(
    ECS::LightComponent::kDefaultDirectionalLightDir,
    direction);
```

如果当前 Quaternion API 没有稳定 FromTo，可用 LookRotation/正交 basis 构造，但必须 golden test。

---

## 4.3 POINT

同步：

```text
position
color
energy * light_intensity_scale
range
radius
```

Ailu `_light_param` 的实际字段语义必须以现有 Inspector/LightingSystem 为准，不凭名字推测。

---

## 4.4 SPOT

同步：

```text
range
inner_angle
outer_angle
direction
```

Blender angle 通常 rad。

Ailu Editor 当前显示/使用 degree，则明确转换：

```text
rad -> degree
```

---

## 4.5 AREA

MVP：

```text
SQUARE
RECTANGLE
```

映射：

```text
width
height
range/intensity
```

DISC/ELLIPSE：

```text
fallback rectangle approximation
warning once
```

---

## 4.6 Light Color

明确：

```text
Blender color
        ×
Blender energy
        ×
user scale
```

如何落到 `_light_color.rgb/a` 按当前 renderer 语义实现。

必须通过简单 scene 校准，而不是仅看代码。

---

## 4.7 Camera Basis

Blender Camera local：

```text
forward = -Z
up = +Y
```

转换到 world，再到 Ailu basis。

Ailu：

```cpp
camera.Position(position);
camera.Rotation(Quaternion::LookRotation(forward, up));
```

不要直接复制 Blender object quaternion。

---

## 4.8 Perspective FOV

Blender发送：

```text
angle_x rad
```

转换：

```text
degree = rad * 180 / pi
```

Ailu：

```text
FovH(degree)
```

---

## 4.9 Aspect

优先使用 Blender scene render resolution：

```text
resolution_x * pixel_aspect_x
resolution_y * pixel_aspect_y
```

而不是 viewport 当前尺寸。

---

## 4.10 Orthographic

Blender：

```text
ortho_scale
```

Ailu：

```text
Camera::Size
```

两者 horizontal/vertical definition 必须通过 golden scene 验证。

不要在未验证前写死 1:1 后直接结束 milestone。

---

# Progress

- [x] SUN。
- [x] POINT。
- [x] SPOT。
- [x] AREA square。
- [x] AREA rectangle。
- [x] direction conversion source path。
- [x] angle conversion。
- [x] intensity scale。
- [x] perspective camera。
- [x] camera forward/up source path。
- [x] FovH degree。
- [x] aspect。
- [x] orthographic。
- [x] type mutation。
- [x] delete cleanup。

源码与 Editor Debug 构建通过；Blender scene orientation、光照和相机 framing 验收未运行。

---

# Decision Log

- semantic vector sync。
- no raw camera quaternion。
- explicit intensity scale。
- rectangle area only exact MVP。

---

# Surprises & Discoveries

重点记录：

- `_light_param` 当前每种 light type 的确切语义。
- `_light_color.a` 的强度路径。
- renderer 是否额外乘 PI。
- ortho `Size` 的确切 axis definition。
- reversed-Z 对 near/far 不应影响 Live Link 参数语义。

---

# Validation

建立：

```text
TargetCube

Sun_Target
Spot_Target
Point_1m
Area_Rect

Camera_50mm
Camera_Wide
Camera_Ortho
```

验证：

- Sun shadow direction。
- Spot cone center 对准 target。
- Point range。
- Area orientation。
- Camera center ray 命中 TargetCube。
- FOV framing。
- near/far。
- ortho framing。

截图可人工对照，但验收结果写进 `progress.md`。

---

# Known Follow-ups

- exact photometric unit。
- disc area。
- camera shift。
- DOF。
- exposure。

---

# Outcomes

M4 后，Blender 已能作为 Ailu 场景布局/灯光/相机前端。

---

# Resume Protocol

如果 Camera/Light 方向还靠“看起来差不多”，M4 不算通过。
