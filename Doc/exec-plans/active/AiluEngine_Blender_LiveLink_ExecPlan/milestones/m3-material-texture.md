# M3 — Principled Material + Texture Sync

# Purpose

将 Blender 常用 Principled BSDF 子集映射为 Ailu Standard Material，并同步其 Texture。

---

# Scope

支持：

- Material UUID。
- material slot binding。
- Base Color scalar。
- Base Color Image。
- Roughness scalar。
- Metallic scalar。
- Normal Map image。
- Emission scalar/image。
- basic IOR / Transmission（在基础 PBR 稳定后）。
- regular image absolute path。
- packed/generated image temp export。
- sRGB / linear usage。
- roughness/metallic packed texture。
- material revision。
- texture revision/cache。

---

# Non-goals

- arbitrary node graph。
- procedural node baking。
- complex UV node graph。
- shader generation。
- SSS。
- clearcoat。
- sheen。
- volume。
- alpha blend full fidelity。
- UDIM 完整支持。
- movie texture。

---

# Architecture Constraints

## Principled Only

找不到受支持 Principled 时：

```text
fallback
warning once
```

不猜复杂 node graph。

## Texture

不创建 Project Asset。

使用：

```cpp
ResourceMgr::LoadExternalTexture(...)
```

## Thread

Texture loading / GPU creation 在 Ailu main Tick。

## Lifetime

Plugin cache 保持：

```text
Ref<Texture2D>
Ref<Material>
```

---

# Baseline

Ailu Standard Material 已有：

```text
_AlbedoValue
_AlbedoTex
_NormalTex
_RoughnessValue
_MetallicValue
_RoughnessMetallicTex
_EmissionValue
_EmissionTex
_IOR
_Transmission
```

---

# Target Outcome

Blender Principled：

```text
BaseColor
Roughness
Metallic
Normal
Emission
```

与 Ailu 视觉基本一致。

修改参数：

```text
无需重新同步 Mesh
```

修改 Image：

```text
Texture reload
```

---

# Relevant Files

```text
Engine/Inc/Render/Material.h
Engine/Src/Render/Material.cpp
Engine/Inc/Render/Texture.h
Engine/Src/Render/Texture.cpp
Engine/Inc/Framework/Common/ResourceMgr.h
Engine/Src/Framework/Common/ResourceMgr.cpp

Plugins/BlenderLiveLink/Inc/LiveLinkMaterialBridge.h
Plugins/BlenderLiveLink/Src/LiveLinkMaterialBridge.cpp
Plugins/BlenderLiveLink/Inc/LiveLinkTextureCache.h
Plugins/BlenderLiveLink/Src/LiveLinkTextureCache.cpp

Tools/Blender/ailu_live_link/material_sync.py
```

---

# Milestones

当前：

```text
M3
```

---

# Current Milestone

M3。

---

# Implementation

## 3.1 Material Identity

```text
Blender Material custom GUID
```

Ailu plugin：

```text
guid -> Ref<Material>
```

Mesh Object message 只引用 material GUID list。

不要用 material name 做缓存 key。

---

## 3.2 Principled Discovery

只处理常见：

```text
Material Output
    ↓
Principled BSDF
```

允许 BaseColor/Normal/Roughness/Metallic 经过简单 Image Texture / Normal Map。

复杂链：

```text
Mix Shader
Math maze
ColorRamp procedural
```

MVP fallback。

---

## 3.3 Base Color

同步：

```text
base_color_factor
base_color_texture_guid(optional)
```

Ailu：

```text
_AlbedoValue
_AlbedoTex
```

---

## 3.4 Roughness + Metallic

先完成 scalar：

```text
_RoughnessValue
_MetallicValue
```

再实现 texture packing。

Ailu texture 约定：

```text
R = roughness
G = metallic
```

Blender addon cache key：

```text
roughness_source_guid/revision
metallic_source_guid/revision
channel selection
```

生成 packed PNG/TGA。

---

## 3.5 Normal

支持：

```text
Image Texture
    ↓
Normal Map
    ↓
Principled Normal
```

如果是 Bump procedural：

```text
MVP unsupported
```

Ailu `_NormalTex` 用 linear。

---

## 3.6 Emission

同步：

```text
emission_color * emission_strength
```

到：

```text
_EmissionValue
```

有 image 时 `_EmissionTex`。

---

## 3.7 Texture Metadata

建议：

```json
{
    "guid": "...",
    "revision": 12,
    "path": "C:/...",
    "usage": "base_color"
}
```

usage：

```text
base_color
normal
roughness_metallic
emission
```

---

## 3.8 Regular Image Path

Blender：

```python
bpy.path.abspath(image.filepath, library=image.library)
```

确保 normalize。

如果文件不存在：

```text
warning
fallback texture
```

---

## 3.9 Packed / Generated

导出：

```text
AiluLiveLinkCache/<session>/<image_guid>_<revision>.png
```

不要覆盖正在被 Ailu 读取的同一个文件。

revision 文件名可以天然避免 file lock/cache confusion。

---

## 3.10 Texture Cache

Ailu：

```text
(texture_guid, revision, usage)
    ↓
Ref<Texture2D>
```

新的 revision load 成功后才替换。

失败继续保留旧 texture。

---

## 3.11 Material Slot Binding

Object mesh submesh：

```text
material_slot index
```

Object metadata：

```text
material_guids[]
```

Ailu：

```text
StaticMeshComponent::_p_mats
```

按 slot 填充。

缺失：

```text
Material::s_checker/default
```

---

# Progress

- [ ] material UUID。
- [ ] Principled discovery。
- [ ] BaseColor factor。
- [ ] Roughness scalar。
- [ ] Metallic scalar。
- [ ] Emission scalar。
- [ ] BaseColor image。
- [ ] Normal Map image。
- [ ] regular image path。
- [ ] packed/generated export。
- [ ] texture usage color space。
- [ ] Texture cache。
- [ ] material slot binding。
- [ ] RM pack。
- [ ] IOR。
- [ ] Transmission。
- [ ] unsupported warning。
- [ ] material revision/coalesce。

---

# Decision Log

- Principled subset。
- transient Material。
- transient external Texture。
- RM packing on Blender side。
- failed texture update keeps previous valid resource。

---

# Surprises & Discoveries

记录：

- `Material::SetTexture` 对非 AssetRegistry Texture 的行为。
- sampler mask 是否自动正确更新。
- path reload 是否受到 ResourceMgr path cache 影响。
- 同一路径内容改变时 `LoadExternalTexture` 是否返回旧 cache。
- 若存在 cache，需要 revision-specific path 或显式 reload。

不要用 persistent `ImportResource` 绕开问题。

---

# Validation

场景：

```text
Mat_Color
Mat_BaseColorTexture
Mat_Metal
Mat_Rough
Mat_Normal
Mat_Emission
Mat_TwoSlot
```

测试：

1. scalar 修改。
2. image 替换。
3. image 文件内容修改。
4. packed image。
5. generated image。
6. material slot reorder。
7. material delete。
8. unsupported node graph fallback。
9. repeated 100 次 material parameter update。

视觉对比：

```text
Blender Material Preview
Ailu Deferred
Ailu PathTracer
```

不要求像素级一致，但不能出现明显通道/色彩空间错误。

---

# Known Follow-ups

- alpha。
- double sided。
- clearcoat。
- SSS。
- advanced UV transform。
- UDIM。
- procedural baking。

---

# Outcomes

M3 后 Live Link 已经覆盖最常见的静态 PBR 资产制作闭环。

---

# Resume Protocol

优先验证 BaseColor + Normal + scalar R/M。

不要一开始就扩展全部 Principled inputs。
