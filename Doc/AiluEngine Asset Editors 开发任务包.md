# AiluEngine Asset Editors 开发任务包

## 1. 目标

为以下资产补充正式的资产编辑器：

- `Render::Mesh`
- `Render::SkeletonMesh`
- `Render::Texture2D`
- `Render::Material`

整体设计参考 UE / Unity 的资产编辑器，但保持 AiluEngine 当前轻量架构，不引入额外重复的数据模型或大型编辑器框架。

最终结构：

```text
AssetEditor
├── MeshAssetEditor
├── SkeletonMeshAssetEditor
├── TextureAssetEditor
└── MaterialAssetEditor
```

公共预览设施：

```text
AssetPreviewViewport3D
├── MeshAssetEditor
├── SkeletonMeshAssetEditor
├── MaterialAssetEditor
└── AnimationClipPreview

TexturePreviewWidget
├── TextureAssetEditor
└── SpriteAssetEditor
```

---

# 2. 当前基础设施

当前仓库已经具备以下设施，应优先复用而不是重新实现。

## AssetEditor

当前已经存在：

```cpp
class AssetEditor : public DockWindow
```

其已经统一负责：

- `Ctrl + S`
- Dirty 状态
- 标题 `*`
- 保存
- Reload
- Discard
- 关闭时 Save / Discard / Cancel
- `OnBeforeSave()`
- `OnAssetSaved()`
- `OnAssetReloaded()`
- `RefreshEditor()`

所有新资产编辑器统一继承 `AssetEditor`。

不要在各 Editor 内重新实现：

```text
_original
_editing
Apply
Revert
Ctrl+S
Close confirmation
```

除非特定资产确实需要独立 Draft 数据。

---

## AnimationClipPreview

当前 `AnimationClipPreview` 已包含大量通用 3D Preview 能力：

- RenderTexture
- Depth
- Camera
- Orbit
- Zoom
- Reset / Focus
- Mesh Bounds framing
- Directional Light
- Grid
- Mesh Draw
- 多 SubMesh
- Skeleton Overlay
- Joint highlight
- SkeletonMesh skinning

不要重新编写另一套 3D Preview。

本任务应从其中抽出通用部分：

```text
AssetPreviewViewport3D
```

---

## Sprite Preview

当前 Sprite Editor 已有：

- Texture 显示
- Pan
- Zoom
- Fit
- 1:1
- Grid
- Alpha Background
- UV / Pivot / Border Overlay

应从 Sprite Preview 中提取通用：

```text
TexturePreviewWidget
```

Sprite 自己只保留：

```text
UV
Pivot
9-slice Border
```

等 Sprite 专属 Overlay。

---

## AssetPreviewGenerator

已有：

```text
AssetPreviewGenerator
```

支持 Mesh / Material / Sprite 等 Asset Browser 缩略图。

该类负责 one-shot thumbnail generation，不要直接改造成交互式资产编辑器 viewport。

但应尽量避免它与新的 `AssetPreviewViewport3D` 继续复制：

- Lighting
- Grid
- Camera constants
- Bounds fitting
- DrawMesh

必要时后续可以下沉公共渲染 helper。

---

# 3. AssetEditorRegistry 调整

当前已有：

```cpp
RegisterEditor<TAsset, TEditor>()
```

建议在实现新资产编辑器时顺便完成 `AssetEditor` 打开流程统一。

目标流程：

```text
AssetEditorRegistry
    ↓
Load asset object if necessary
    ↓
TEditor::Open(Asset*)
    ↓
AssetEditor::BindAsset(...)
    ↓
OnOpen()
```

新 Editor 不再增加：

```cpp
void Open(Render::Mesh*);
void Open(Render::Material*);
```

这类 typed transitional API。

推荐形式：

```cpp
class MeshAssetEditor final : public AssetEditor
{
protected:
    bool OnOpen() override
    {
        _mesh = GetAssetObject<Render::Mesh>();
        return _mesh != nullptr;
    }

private:
    Render::Mesh* _mesh = nullptr;
};
```

---

# 4. Registry 保持 Exact Type Matching

本阶段不要实现 BaseType 自动 fallback。

显式注册：

```text
Mesh         -> MeshAssetEditor
SkeletonMesh -> SkeletonMeshAssetEditor
Texture2D    -> TextureAssetEditor
Material     -> MaterialAssetEditor
```

原因：

`Texture` 基类下可能同时存在：

- Texture2D
- Texture3D
- CubeMap
- RenderTexture

其中部分是 runtime resource，并非持久化可编辑 Asset。

因此当前：

```text
Asset Type == Editor Type
```

采用精确注册更加安全。

---

# 5. ImportSettings 设计

## 原则

不要创建：

```text
TextureImportSettingsDocument
MeshImportSettingsDocument
```

等专门的重复 Document 类型。

当前 ImportSetting 成员基本都是：

- bool
- enum
- String
- integer
- float

等稳定基础数据。

因此直接序列化 runtime ImportSetting 本身。

例如：

```cpp
ASTRUCT()
struct TextureImportSetting
{
    GENERATED_BODY()

    APROPERTY()
    bool _is_sRGB = true;

    APROPERTY()
    bool _generate_mipmap = true;

    APROPERTY()
    bool _is_readable = false;
};
```

Asset 描述中直接保存：

```cpp
ASTRUCT()
struct Texture2DAssetDocument
{
    GENERATED_BODY()

    APROPERTY()
    String _file;

    APROPERTY()
    TextureImportSetting _import_setting;
};
```

Mesh 同理：

```cpp
ASTRUCT()
struct MeshAssetDocument
{
    GENERATED_BODY()

    APROPERTY()
    String _file;

    APROPERTY()
    MeshImportSetting _import_setting;
};
```

---

# 6. ImportSetting 生命周期

ImportSetting 应作为导入配置的唯一 Source of Truth：

```text
.alasset
   ↓
ImportSetting
   ↓
Importer
   ↓
ImportSetting Hash
   ↓
Artifact Key
   ↓
Library / DerivedData
```

编辑 ImportSetting：

```text
修改配置
 ↓
Save asset metadata
 ↓
Reimport
 ↓
重新计算 import_setting_hash
 ↓
Artifact miss
 ↓
重新构建 DerivedData
```

ImportSetting 内不要加入：

```text
GpuResource*
Asset*
Importer*
callback
file handle
temporary runtime cache
```

它应保持：

```text
Plain Serializable Configuration
```

---

# 7. Reimport API

建议补充 Editor-facing 的统一接口：

```cpp
ResourceMgr::ReimportAsset(Asset* asset);
```

职责：

```text
读取最新 ImportSetting
 ↓
重新计算 ArtifactKey
 ↓
尝试 DDC
 ↓
若 miss 则重新 Import
 ↓
生成 Artifact
 ↓
创建 / 更新 Runtime Object
 ↓
刷新已打开 Editor
```

资产编辑器不要分别调用：

```text
SetImportSetting
Unload
Load
InvalidateArtifact
Reload
```

等底层步骤。

统一由 `ReimportAsset()` 管理。

---

# 8. AssetPreviewViewport3D

从 `AnimationClipPreview` 中提取通用 3D Preview。

建议结构：

```cpp
class AssetPreviewViewport3D
{
public:
    void SetMesh(Render::Mesh* mesh);
    void SetMaterial(Render::Material* material);

    void Focus();
    void ResetCamera();

    void SetShowGrid(bool value);
    void SetWireframe(bool value);

    void Draw(...);

private:
    RenderTexture ...
    DepthTexture ...

    Camera ...
    OrbitController ...

    Render::Mesh* _mesh;
    Render::Material* _material;
};
```

应提供：

- Orbit
- Pan（若现有 Preview 尚未支持，可补）
- Zoom
- Focus Bounds
- Reset Camera
- Grid
- Background
- Lighting
- Mesh Rendering
- Multi-submesh
- Wireframe
- Overlay hook

例如：

```cpp
SetOverlayCallback(...)
```

允许 Skeleton Editor / Animation Clip Editor 绘制骨骼。

---

# 9. AnimationClipPreview 重构

重构为组合：

```text
AnimationClipPreview
├── AssetPreviewViewport3D
├── SkeletonAnimationBinding
├── SkeletonPose
├── Skinning
└── Skeleton Overlay
```

不要再自己拥有另一套：

```text
Camera
RenderTexture
Grid
Lighting
Mesh Draw
```

完成后需要确保现有 AnimationClip Editor 行为保持不变。

---

# 10. MeshAssetEditor

## 定位

Mesh 为 Imported Asset。

Editor 主要负责：

```text
Viewer
+
Information
+
Import Settings
+
Reimport
```

不直接修改 Mesh runtime geometry。

---

## UI

建议布局：

```text
┌─────────────────────────────────────────────┐
│ Save | Reimport | Focus | Grid | Wireframe  │
├──────────────────────────────┬──────────────┤
│                              │ Source       │
│                              │              │
│        3D Preview            │ Import       │
│                              │ Settings     │
│                              │              │
│                              │ Mesh Info    │
│                              │ Vertices     │
│                              │ Triangles    │
│                              │ Submeshes    │
│                              │ UV Channels  │
│                              │ Bounds       │
│                              │ Derived Data │
└──────────────────────────────┴──────────────┘
```

---

## Mesh Info

显示只读信息：

```text
Vertex Count
Triangle Count
Submesh Count
UV Channel Count
Bounds
Normals available
Tangents available
Colors available
DerivedData status
```

可选显示：

```text
Artifact Key
Artifact Size
Source Path
```

---

## Import Settings

直接编辑：

```cpp
MeshImportSetting
```

通过反射属性面板绘制。

修改后：

```text
MarkDirty
```

保存后：

```text
Reimport
```

第一版可允许：

```text
Save
Reimport
```

分开操作。

不要在 property changed 时实时重导。

---

## Non-goals

第一版不实现：

- Vertex Editing
- Mesh Simplification
- LOD Authoring
- Collision Editing
- Nanite-like systems

---

# 11. TexturePreviewWidget

从 Sprite Preview 抽取：

```text
Texture Preview
├── Pan
├── Zoom
├── Fit
├── 1:1
├── Checkerboard
├── RGBA Channels
├── Mip
└── Pixel Grid
```

建议接口：

```cpp
class TexturePreviewWidget : public UIElement
{
public:
    void SetTexture(Render::Texture* texture);

    void SetMip(uint32_t mip);
    void SetChannelMask(...);

    void Fit();
    void ResetZoom();

    void SetShowCheckerboard(bool);
    void SetShowPixelGrid(bool);
};
```

允许外部注册额外 overlay：

```cpp
OnDrawOverlay(...)
```

Sprite Editor 使用 Overlay 绘制：

```text
UV Rect
Pivot
Border
```

---

# 12. TextureAssetEditor

## 定位

Texture 同样作为 Imported Asset：

```text
Viewer
+
Information
+
Import Settings
+
Reimport
```

---

## UI

```text
┌──────────────────────────────────────────────┐
│ Save | Reimport | RGBA | Mip | Fit | 1:1    │
├───────────────────────────────┬──────────────┤
│                               │ Source       │
│                               │              │
│       Texture Preview         │ Import       │
│                               │ Settings     │
│                               │              │
│                               │ Information  │
│                               │ Size         │
│                               │ Format       │
│                               │ Mips         │
│                               │ Memory       │
└───────────────────────────────┴──────────────┘
```

---

## Information

只读显示：

```text
Width
Height
PixelFormat
Mip Count
Dimension
GPU Memory estimate
Readable
sRGB
```

---

## Import Settings

直接使用：

```cpp
TextureImportSetting
```

例如：

```text
sRGB
Generate Mipmaps
Readable
```

如果当前某些字段尚未正确进入 Asset Serialization，应先补持久化。

不要把 runtime-only：

```text
FilterMode
WrapMode
```

误当 ImportSetting，除非明确决定让它们成为持久化资产属性。

---

# 13. SkeletonMeshAssetEditor

复用：

```text
AssetPreviewViewport3D
+
AnimationClipPreview 中已有 Skeleton Overlay
```

建议布局：

```text
┌─────────────────────────────────────────────┐
│ Focus | Grid | Skeleton | Preview Animation │
├──────────────┬─────────────────┬────────────┤
│ Skeleton     │                 │ Details    │
│ Tree         │   3D Preview    │            │
│              │                 │ Mesh Info  │
│ Root         │                 │ Skin Info  │
│ ├ Bone       │                 │ Bone Info  │
│ └ Bone       │                 │            │
└──────────────┴─────────────────┴────────────┘
```

---

## Skeleton Tree

显示：

```text
Bone hierarchy
Selected bone
Parent
Children
Bind transform
Current preview transform
```

点击 Bone：

```text
Skeleton Tree
   ↓
Selected Bone
   ↓
Preview joint highlight
```

---

## Preview Animation

第一版只需要：

```text
AnimationClip selector
Play
Pause
Stop
Loop
Timeline time
```

直接复用现有 animation evaluation / skinning 设施。

---

## Non-goals

第一版不实现：

- Skeleton Editing
- Retargeting
- Socket Authoring
- Physics Asset
- Clothing
- Morph authoring

---

# 14. MaterialAssetEditor

## 设计原则

当前只有：

```cpp
Render::Material
```

不要重新引入：

```text
StandardMaterial subclass
```

Material Editor 基于：

```text
Material
+
Shader metadata
```

动态构建属性 UI。

---

## Shader Property

利用现有：

```text
ShaderPropertyInfo
```

支持：

```text
Bool
Float
Range
Vector
Color
Texture2D
Texture3D
Enum
```

映射至对应编辑控件。

结构：

```text
Material
   ↓
Shader
   ↓
Shader Properties
   ↓
MaterialPropertyPanel
```

添加新的 Shader Property 时不应要求修改 `MaterialAssetEditor`。

---

## UI

```text
┌──────────────────────────────────────────────┐
│ Save | Sphere | Cube | Plane | Grid | Reset │
├───────────────────────────────┬──────────────┤
│                               │ Shader       │
│                               │              │
│       Material Preview        │ Surface      │
│                               │ Render State │
│                               │              │
│                               │ Properties   │
│                               │ Textures     │
│                               │ Keywords     │
└───────────────────────────────┴──────────────┘
```

---

## Preview Mesh

支持：

```text
Sphere
Cube
Plane
```

后续可增加：

```text
Custom Mesh
```

---

## Material Properties

至少支持：

```text
Shader
Render Queue
Cull Mode
Keywords
Shader Properties
Texture Slots
```

---

## Standard Lit

当前 `Material` 自身已有：

```cpp
IsStandardLit()
```

以及：

```text
SurfaceType
StandardMaterialProperty
ETextureUsage
```

因此可为 Standard Lit 增加更友好的语义分组：

```text
Surface
Albedo
Normal
Metallic
Roughness
Emission
```

但底层仍然只修改：

```cpp
Render::Material
```

不要创建 StandardMaterial Editor 或 Material 子类分支体系。

---

# 15. Material Graph

本任务禁止实现 Material Graph。

当前架构是：

```text
Material
+
Shader
+
ShaderProperty
+
Keywords
+
Render State
```

尚不存在完整：

```text
Material Graph
 ↓
IR
 ↓
Shader Generator
```

Graph Editor 即使已经存在，也不要强行与 Material Editor 集成。

Material Graph 单独作为后续任务。

---

# 16. ReflectedPropertyPanel 使用原则

已有：

```text
ReflectedPropertyPanel
```

应优先用于：

```text
ImportSetting
普通 APROPERTY Struct/Object
```

例如：

```text
MeshImportSetting
TextureImportSetting
```

直接：

```text
Reflection
 ↓
ReflectedPropertyPanel
```

Material ShaderProperty 不属于普通 C++ reflection property，应使用单独的：

```text
MaterialPropertyPanel
```

根据 `ShaderPropertyInfo` 动态生成控件。

不要试图把所有 Details 面板统一塞进一个万能系统。

---

# 17. 推荐开发阶段

## Phase 0 — AssetEditor Registry Cleanup

完成：

- 新 Editor 统一 `AssetEditor`
- Registry 使用 `Open(Asset*)`
- 新 Editor 使用 `OnOpen()`
- 减少 typed `Open(T*)`

---

## Phase 1 — ImportSetting Persistence

完成：

```text
TextureImportSetting
MeshImportSetting
```

直接进入 Asset metadata 序列化。

保证：

```text
保存
关闭 Editor
重启
重新加载
```

ImportSetting 不丢失。

同时实现：

```cpp
ResourceMgr::ReimportAsset(...)
```

---

## Phase 2 — AssetPreviewViewport3D

从：

```text
AnimationClipPreview
```

提取公共：

```text
RenderTexture
Camera
Orbit
Zoom
Focus
Grid
Lighting
Bounds fitting
Mesh rendering
```

AnimationClipPreview 改为组合。

---

## Phase 3 — MeshAssetEditor

完成：

```text
3D Preview
Mesh Info
Import Settings
Reimport
Wireframe
Focus
Grid
```

---

## Phase 4 — TexturePreviewWidget

从 Sprite Preview 提取：

```text
Texture drawing
Pan
Zoom
Fit
1:1
Channels
Mip
Checkerboard
Pixel grid
```

保持 Sprite Editor 行为不变。

---

## Phase 5 — TextureAssetEditor

完成：

```text
Texture Preview
Information
Import Settings
Reimport
```

---

## Phase 6 — SkeletonMeshAssetEditor

完成：

```text
3D Preview
Skeleton Tree
Bone Highlight
Skeleton Overlay
Mesh/Skin Info
Preview Animation
```

---

## Phase 7 — MaterialAssetEditor

完成：

```text
Preview Mesh
Shader selector
Dynamic Shader Properties
Textures
Keywords
Render State
Standard Lit semantic grouping
```

---

# 18. 验收标准

## Common

四种资产均能够从 Asset Browser 双击打开对应 Editor：

```text
Mesh
SkeletonMesh
Texture2D
Material
```

Editor：

- 正确绑定 Asset
- Dirty 状态正确
- Ctrl+S 正常
- Close confirmation 正常
- Dock Window 正常
- 同一 Asset 不重复生成异常实例

---

## Mesh

- 正确预览 Mesh
- Orbit / Zoom / Focus 正常
- 支持多 SubMesh
- Stats 正确
- ImportSetting 可编辑、保存
- Reimport 后 Preview 更新
- DDC / Artifact 流程正常

---

## Texture

- 正确显示 Texture
- Pan / Zoom / Fit / 1:1 正常
- RGBA channel 正常
- Mip selection 正常
- ImportSetting 可持久化
- Reimport 后更新
- Sprite Editor 不产生功能退化

---

## SkeletonMesh

- 正确显示蒙皮网格
- Skeleton Tree 正确
- Bone hierarchy 正确
- Bone selection 与 Preview highlight 联动
- Skeleton overlay 正确
- AnimationClip 可用于 Preview
- 播放过程 skinning 正确

---

## Material

- Sphere / Cube / Plane Preview 正常
- Shader 切换正常
- ShaderProperty 根据 metadata 自动生成
- Bool / Float / Range / Vector / Color / Texture / Enum 正常
- Keyword 操作正常
- RenderQueue / CullMode 正常
- Standard Lit 分组可选
- 不依赖任何 `StandardMaterial` 类

---

# 19. 明确禁止

本任务不要：

- 新建 `StandardMaterial`
- 新建 `MeshEditorBase`
- 新建 `MaterialEditorBase`
- 新建 `TextureImportSettingsDocument`
- 新建 `MeshImportSettingsDocument`
- 再写一套 Asset save / dirty 生命周期
- 再写一套 Animation Preview Camera
- 再写一套 Sprite Texture Canvas
- 为 Registry 提前加入 BaseType fallback
- 实现 Material Graph
- 实现 Mesh vertex editing
- 实现 LOD authoring
- 实现 Skeleton retargeting

原则：

> 优先抽取当前仓库已经证明可用的代码，并通过组合提供给新资产编辑器，而不是重新构建第二套平行框架。