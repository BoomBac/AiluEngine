# AiluEngine Blender Live Link ExecPlan — 系统总览

> 基线：`AiluEngine_Source(20260930-154655).zip`
>
> 参考结构：`AiluEngine_Plugin_System_ExecPlan_Split(1).zip`
>
> 状态：Active
>
> 当前里程碑：M0 — Plugin Scaffold + Loopback Transport
>
> 本执行计划假定 **AiluEngine 当前插件系统已经完成并可用**，不重新实现 `PluginManager`。

---

# Purpose

为 AiluEngine 实现一个 Blender → AiluEngine 的单向 Live Link。

第一阶段只同步：

- Object / Transform / Hierarchy
- Static Mesh
- Material
- Texture
- Light
- Camera

目标不是做完整的 DCC interchange framework，而是让 Blender 成为 AiluEngine 的高效场景搭建与资产预览前端：

```text
Blender
    │
    │ Live Link
    ▼
AiluEngine Editor
```

典型工作流：

```text
Blender 中移动对象
        ↓
Ailu Editor 下一次同步 Tick 更新 Transform

Blender 修改 Modifier / Mesh
        ↓
重新提取 evaluated mesh
        ↓
发送二进制 Mesh Payload
        ↓
Ailu 重建 transient Render::Mesh

Blender 修改 Principled BSDF
        ↓
发送受支持的 PBR 参数
        ↓
Ailu transient Material 更新

Blender 调灯光 / 相机
        ↓
Ailu LightComponent / CCamera 更新
```

Live Link 必须作为 **现有动态插件系统上的 Editor Plugin** 存在，并可以安全加载、运行和卸载。

---

# Scope

MVP 包含：

1. `BlenderLiveLink` Ailu Editor 插件。
2. 本机 loopback TCP。
3. 固定版本 Live Link wire protocol。
4. Blender Python Add-on。
5. Blender persistent UUID。
6. Full Sync。
7. Incremental Sync。
8. Transform / hierarchy。
9. Evaluated static mesh。
10. Principled BSDF 的约束子集。
11. Texture path / packed image 临时导出。
12. Directional / Point / Spot / Rectangle Area Light。
13. Perspective / Orthographic Camera。
14. Reconnect / Clear Session。
15. 插件安全卸载。
16. 基础压力测试与手工视觉验收。

第一版明确是：

```text
Blender -> AiluEngine
```

不是双向编辑。

---

# Non-goals

本轮不要实现：

- AiluEngine → Blender 反向同步。
- Skeletal Mesh。
- Skeleton / Bone。
- Animation。
- Shape Key。
- Geometry Nodes 节点图翻译。
- Blender 任意 Shader Node Graph 翻译。
- Ailu Shader Graph 自动生成。
- Particle / Hair。
- Volume。
- World / HDRI 环境完整同步。
- Collection → Prefab 自动转换。
- Asset Library。
- 跨机器 Live Link。
- LAN 监听。
- TLS / 身份认证。
- 通用文件传输服务。
- Runtime Player Live Link。
- 插件定义新的 ECS Component。
- 插件动态注册新的反射类型。
- Editor 私有类型跨 DLL ABI。
- 依赖 FBX 导出完成 Live Link。
- 依赖 `.glb` 作为实时 Mesh 传输格式。
- 为了 Live Link 重写当前 PluginManager。
- 为了 Live Link 重写整个 Asset Pipeline。

如果后续有实际需求，再从该基础上扩展。

---

# Architecture Constraints

## 1. 必须复用现有插件系统

当前源码已经包含：

```text
Engine/Inc/Framework/Plugin/PluginApi.h
Engine/Inc/Framework/Plugin/PluginManager.h
Engine/Inc/Framework/Plugin/PluginManifest.h
Engine/Inc/Framework/Plugin/PluginRegistration.h

Engine/Src/Framework/Plugin/PluginManager.cpp
Engine/Src/Framework/Plugin/PluginManifest.cpp

Engine/Inc/Framework/Platform/DynamicLibrary.h
Engine/Src/Framework/Platform/DynamicLibrary.cpp
```

已经具备：

```text
DynamicLibrary
Plugin API Version
Manifest Scan
Autoload
PluginModule
PluginManager
PluginRegistrationScope
Load / Tick / Unload / Shutdown
```

因此 Blend Link 的任务是：

```text
Existing Plugin Host
        +
BlenderLiveLinkPlugin
```

而不是再次实现插件框架。

---

## 2. 不升级 Plugin API Version

当前：

```cpp
inline constexpr u32 kPluginApiVersion = 1u;
```

Live Link MVP 不需要修改 `IPlugin` ABI。

插件可以：

```text
Plugin.dll
   ↓
Engine.dll public API
```

直接使用 Engine 导出的：

- `SceneMgr`
- `Scene`
- ECS Register
- `Render::Mesh`
- `Render::Material`
- `Texture2D`
- `ResourceMgr`

禁止依赖 Editor 私有实现：

```text
Plugin.dll -> Editor.exe private class
```

不要 include：

```text
EditorApp private implementation
DockManager private implementation
EditorLayer private implementation
```

---

## 3. `OnLoad()` 不能访问 Scene / Graphics

当前 `EditorApp::Initialize()` 的顺序很重要：

```text
Application::Initialize
        ↓
PluginManager::Initialize
        ↓
ScanPlugins
        ↓
LoadAutoloadPlugins
        ↓
InputLayer
        ↓
RenderPipeline
        ↓
OpenScene
        ↓
LoadEditorResource
        ↓
进入 Application main loop
```

也就是说：

> `BlenderLiveLinkPlugin::OnLoad()` 发生时，Active Scene 和部分 Editor/Render 状态尚未准备完成。

所以 `OnLoad()` 只允许：

```text
初始化 CPU-only state
创建 socket server
启动 network thread
创建 thread-safe command queue
写日志
```

禁止：

```text
SceneMgr::Get().ActiveScene()
ResourceMgr GPU resource creation
RenderPipeline access
Editor UI creation
```

真正应用 Live Link 数据必须在：

```cpp
BlenderLiveLinkPlugin::Tick(float delta_time)
```

中完成。

此时已经进入主循环。

---

## 4. Engine 只允许主线程修改

Network worker：

```text
recv packet
decode header
validate payload
copy immutable payload
push command queue
```

Network worker 禁止：

```text
访问 Scene
创建 Entity
修改 ECS
创建 Mesh
上传 GPU
LoadExternalTexture
修改 Material
```

插件 Tick：

```text
main thread
    ↓
drain queue
    ↓
coalesce
    ↓
apply Scene / Mesh / Material / Texture / Light / Camera
```

这是 Live Link 最重要的线程边界。

---

## 5. 第一版只绑定 loopback

默认：

```text
127.0.0.1:17777
```

禁止：

```text
0.0.0.0
LAN IP
Internet
```

原因：

- MVP 无认证。
- Live Link payload 可以驱动场景修改。
- Texture message 包含本机文件路径。
- 跨机器没有必要。

后续如果需要 remote link，再单独设计权限和 blob transfer。

---

## 6. 插件卸载必须停止所有线程

现有插件系统最终会：

```text
RegistrationScope.Close
        ↓
IPlugin::OnUnload
        ↓
AiluPluginDestroy
        ↓
FreeLibrary
```

因此 `OnUnload()` 必须保证：

```text
停止 accept
关闭 client socket
设置 stop flag
唤醒 worker
join worker
清空 command queue
清理 transient scene session
释放 transient resource cache
返回
```

禁止 detached thread。

---

## 7. Blender Python API 只能在 Blender 主线程读数据

Depsgraph handler 只做 dirty collection。

网络线程不能调用：

```python
bpy.data
bpy.context
obj.evaluated_get(...)
mesh.to_mesh(...)
```

推荐：

```text
depsgraph_update_post
        ↓
mark dirty IDs
        ↓
bpy.app.timers periodic flush
        ↓
在 Blender main thread 构建 payload
        ↓
send queue
        ↓
socket worker 只发送 bytes
```

---

## 8. Live Link Entity 必须是 transient

当前 Ailu Scene 保存流程会遍历普通场景实体并序列化。

如果 Live Link 直接：

```cpp
scene->AddObject(...)
```

然后用户 Save Scene，Live Link 预览对象也会写入 Scene Asset。

这是不可接受的。

M1 必须增加一个 **Engine-owned transient entity 语义**。

最小方案：

```cpp
struct TagComponent
{
    ...
    bool _is_transient = false;
};
```

要求：

- `_is_transient` 不序列化。
- Live Link root 与所有 Live Link child 都设为 true。
- Scene Asset 保存时跳过 transient entity。
- 插件卸载/断连可删除整个 transient subtree。

不要用：

```text
LiveLinkPluginComponent
```

因为动态 DLL 定义 ECS Component 会引入组件类型生命周期与反射卸载问题。

---

## 9. Play Mode / Temporary Scene 暂停同步

MVP 不修改运行时复制场景。

当：

```text
Application::_is_playing_mode == true
```

或当前是 temporary scene 时：

```text
停止 Apply
继续接收但丢弃/压缩为 resync-needed
```

回到 Edit Scene：

```text
request full sync
```

不要尝试在第一版同时维护 Editor Scene 与 Runtime Scene。

---

# Baseline

## Plugin System

当前插件系统已经支持本任务所需的基础能力。

`PluginApi.h`：

```text
kPluginApiVersion = 1
EPluginType
PluginDescriptor
IPluginHost
PluginContext
IPlugin
```

`PluginManager` 当前支持：

```text
Initialize
Shutdown
ScanPlugins
LoadAutoloadPlugins
LoadPlugin(path)
LoadPlugin(id)
UnloadPlugin
Tick
DiscoveredPlugins
owner registration
```

无需修改 ABI。

---

## Editor Integration

当前 Editor 已：

```text
创建 PluginManager
扫描 AiluRoot/Plugins
扫描 ProjectRoot/Plugins
加载 autoload plugin
_before_update -> PluginManager::Tick
Finalize 前 Shutdown PluginManager
```

Blend Link 可以直接以：

```json
{
    "id": "com.ailu.blender_livelink",
    "name": "Blender Live Link",
    "version": "0.1.0",
    "api_version": 1,
    "type": "editor",
    "library": "AiluBlenderLiveLink.dll",
    "autoload": true
}
```

部署。

MVP 推荐 `autoload=true`，因为当前 Editor 还没有完整 Plugin Manager UI。

---

## Scene

现有 `Scene` 已提供足够的 public API：

```text
AddObject
RemoveObject
Reparent
Detach
RenameEntity

FindEntity(Guid)
GetEntityGuid
HasEntityGuid

GetRegister

MarkEdited
MarkStructureChanged
MarkDirty
```

并且每个 scene entity 已有 `PersistentIdComponent`。

因此 Blender Object UUID 可以直接映射为 Ailu Entity Guid。

---

## Transform

`TransformComponent` 已提供：

```text
SetLocalPosition
SetLocalRotation
SetLocalScale
SetLocalTransform
```

不要直接修改内部 world matrix。

---

## Mesh

现有 `Render::Mesh` 支持：

```text
SetVertices
SetNormals
SetTangents
SetUVs
AddSubmesh
SetBounds
Apply
```

因此 Live Link 可以直接构建 transient runtime Mesh，不需要经过 Asset 文件。

---

## Material

现有标准材质已经覆盖本任务所需的 PBR 参数：

```text
_AlbedoTex
_NormalTex
_RoughnessMetallicTex
_SpecularTex
_EmissionTex

_AlbedoValue
_EmissionValue
_SpecularValue
_RoughnessValue
_MetallicValue
_Anisotropy
_MaterialID
_AlphaCulloff
_IOR
_Transmission
_AttenuationColor
_AttenuationDistance
```

MVP 只做 Principled BSDF 映射。

---

## Texture

现有：

```cpp
ResourceMgr::LoadExternalTexture(...)
```

非常适合 Live Link transient texture。

Live Link 不需要把 Blender texture 导入 Project Asset DB。

---

## Light

当前：

```cpp
enum class ELightType
{
    kDirectional,
    kPoint,
    kSpot,
    kArea
};
```

与 Blender：

```text
SUN
POINT
SPOT
AREA
```

高度匹配。

注意：

- Ailu 当前 area light 的实际 world-state 计算主要按矩形宽高生成 corners。
- Blender DISC / ELLIPSE 暂时不要声称物理一致。

---

## Camera

Ailu Camera 已支持：

```text
Perspective
Orthographic
FovH
Near
Far
Size
Aspect
```

重要：

```text
Ailu FovH 使用 degree
Blender camera angle 使用 radian
```

---

# Target Outcome

完成 M0 ~ M6 后：

```text
Blender Add-on
    │
    ├── Connect / Disconnect
    ├── Full Sync
    ├── Auto Sync
    └── status
    │
    ▼
127.0.0.1 TCP
    │
    ▼
AiluBlenderLiveLink.dll
    │
    ├── LiveLinkServer
    ├── LiveLinkProtocol
    ├── LiveLinkCommandQueue
    ├── LiveLinkSession
    ├── LiveLinkSceneBridge
    ├── LiveLinkMeshBridge
    ├── LiveLinkMaterialBridge
    └── LiveLinkTextureCache
    │
    ▼
AiluEngine public Engine API
```

用户体验：

```text
启动 Ailu Editor
启动 Blender
Blender: Connect
Blender: Full Sync
        ↓
Ailu 出现 LiveLink transient root
        ↓
编辑 Blender
        ↓
Ailu 实时更新
```

断开：

```text
Disconnect
    ↓
可配置 Clear Session
```

卸载插件：

```text
Disable/Unload
    ↓
socket stop
worker join
transient scene cleanup
resources release
DLL unload
```

---

# Relevant Files

## 现有插件系统

```text
Engine/Inc/Framework/Plugin/PluginApi.h
Engine/Inc/Framework/Plugin/PluginManager.h
Engine/Inc/Framework/Plugin/PluginManifest.h
Engine/Inc/Framework/Plugin/PluginRegistration.h

Engine/Src/Framework/Plugin/PluginManager.cpp
Engine/Src/Framework/Plugin/PluginManifest.cpp

Engine/Inc/Framework/Platform/DynamicLibrary.h
Engine/Src/Framework/Platform/DynamicLibrary.cpp

Editor/Src/EditorApp.cpp
Editor/Src/EditorMain.cpp
```

## Scene / ECS

```text
Engine/Inc/Scene/Scene.h
Engine/Inc/Scene/Component.h

Engine/Src/Scene/Scene.cpp
Engine/Src/Scene/RenderSystem.cpp
Engine/Src/Scene/EntitySerializer.cpp
Engine/Src/Assets/AssetHandlers.cpp
```

## Render Resource

```text
Engine/Inc/Render/Mesh.h
Engine/Src/Render/Mesh.cpp

Engine/Inc/Render/Material.h
Engine/Src/Render/Material.cpp

Engine/Inc/Render/Texture.h
Engine/Src/Render/Texture.cpp

Engine/Inc/Framework/Common/ResourceMgr.h
Engine/Src/Framework/Common/ResourceMgr.cpp
```

## Camera

```text
Engine/Inc/Render/Camera.h
Engine/Src/Render/Camera.cpp
```

## Parser，仅作为参考

```text
Engine/Src/Framework/Parser/GltfParser.cpp
Engine/Src/Framework/Parser/FbxParser.cpp
```

Live Link Mesh MVP 不依赖这两个 Parser。

---

# Proposed Files

建议新增：

```text
Plugins/
└── BlenderLiveLink/
    ├── CMakeLists.txt
    ├── plugin.json
    ├── Inc/
    │   ├── BlenderLiveLinkPlugin.h
    │   ├── LiveLinkProtocol.h
    │   ├── LiveLinkServer.h
    │   ├── LiveLinkCommandQueue.h
    │   ├── LiveLinkSession.h
    │   ├── LiveLinkSceneBridge.h
    │   ├── LiveLinkMeshBridge.h
    │   ├── LiveLinkMaterialBridge.h
    │   └── LiveLinkTextureCache.h
    └── Src/
        ├── BlenderLiveLinkPlugin.cpp
        ├── LiveLinkProtocol.cpp
        ├── LiveLinkServer.cpp
        ├── LiveLinkSession.cpp
        ├── LiveLinkSceneBridge.cpp
        ├── LiveLinkMeshBridge.cpp
        ├── LiveLinkMaterialBridge.cpp
        └── LiveLinkTextureCache.cpp
```

Blender side：

```text
Tools/
└── Blender/
    └── ailu_live_link/
        ├── __init__.py
        ├── addon.py
        ├── connection.py
        ├── protocol.py
        ├── dirty_tracker.py
        ├── coordinate.py
        ├── object_sync.py
        ├── mesh_sync.py
        ├── material_sync.py
        ├── light_sync.py
        ├── camera_sync.py
        └── ui.py
```

如果仓库已有统一插件/工具目录约定，实施时沿用现有约定，不为了此任务移动大量目录。

---

# Wire Protocol

## Packet Header

建议 little-endian：

```cpp
struct LiveLinkPacketHeader
{
    u32 _magic = kLiveLinkMagic;
    u16 _version = kLiveLinkProtocolVersion;
    u16 _type = 0u;
    u32 _payload_size = 0u;
    u64 _sequence = 0u;
};
```

常量：

```cpp
inline constexpr u32 kLiveLinkMagic = 0x4B4C4C41u; // "ALLK" little-endian
inline constexpr u16 kLiveLinkProtocolVersion = 1u;
inline constexpr u32 kMaxLiveLinkPayloadSize = 256u * 1024u * 1024u;
```

所有 packet：

```text
Header
Payload
```

必须：

- exact read。
- 校验 magic。
- 校验 version。
- 校验 payload size。
- malformed packet 关闭当前 client。
- 不允许一次 payload 分配无上限内存。

---

## Message Types

建议：

```text
kHello
kHelloAck
kPing
kPong

kFullSyncBegin
kFullSyncEnd
kClearSession

kObjectUpsert
kObjectDelete

kMeshUpdate
kMaterialUpdate
kTextureUpdate
kLightUpdate
kCameraUpdate
```

MVP 不追求消息类型数量最少，优先可调试性。

---

## JSON vs Binary

以下使用 JSON：

```text
Hello
Full Sync markers
Object
Transform / Parent
Material metadata
Texture metadata
Light
Camera
```

Mesh 使用 binary。

原因：

```text
100k triangle mesh
```

不应该被编码成巨大的 JSON 数组。

---

# Identity Model

## Blender UUID

不要使用 name 作为主键。

Object：

```python
obj["ailu_live_link_guid"]
```

Material：

```python
material["ailu_live_link_guid"]
```

Image：

```python
image["ailu_live_link_guid"]
```

Scene：

```python
scene["ailu_live_link_session_guid"]
```

不存在时：

```python
str(uuid.uuid4())
```

写回 datablock custom property。

---

## Ailu Entity

Object GUID：

```text
Blender Object UUID
        ↓
Ailu Entity PersistentId Guid
```

Full Sync 和 Incremental Sync 使用同一个 ID。

Rename：

```text
只修改 Tag/name
Guid 不变
```

Duplicate：

```text
必须获得新 GUID
```

如果 Blender duplicate 连 custom property 一起复制，addon 必须检测同一 scene 中 GUID collision 并重新生成。

---

# Coordinate System

这是必须单独实现和单独测试的模块。

不要在各 exporter 中散落：

```python
(x, z, y)
```

或手写 Euler 变换。

提供统一：

```text
BlenderToAiluPosition
BlenderToAiluDirection
BlenderToAiluMatrix
BlenderToAiluRotation
```

已知：

```text
Blender: right-handed, Z-up
Ailu: Y-up, +Z forward
```

对于普通 vector basis，当前代码路径表明可以使用确定的 basis matrix 做坐标转换，但 Camera/Light 的 local forward axis 与普通 Object 不能只靠 Euler 分量交换。

Camera：

```text
Blender local forward = -Z
Ailu camera forward = +Z
```

应同步 semantic：

```text
world_forward
world_up
```

然后：

```cpp
Quaternion::LookRotation(forward, up)
```

Light：

```text
Blender SUN/SPOT direction = local -Z
Ailu Light default direction = local -Y
```

应同步方向，然后由 Ailu 建立 rotation。

M1/M4 必须有 golden scene 验证：

```text
identity
+X
+Y
+Z
90° rotations
parented transform
negative scale
camera looking exact target
sun looking exact target
```

---

# Mesh Strategy

## 为什么不使用 FBX / glTF 中转

当前源码检查得到：

1. Ailu 已经可以直接构造 `Render::Mesh`。
2. 当前 glTF parser 实时材质映射并不完整。
3. 当前路径不适合作为每次小几何修改的 Live Link 中转层。
4. 文件导出 + watcher + importer 会引入大量不必要的状态。
5. evaluated mesh 可以直接由 Blender 提取。

所以 MVP：

```text
Blender evaluated mesh
        ↓
custom binary payload
        ↓
Ailu transient Render::Mesh
```

---

## Blender Mesh Extraction

使用 evaluated depsgraph：

```python
depsgraph = bpy.context.evaluated_depsgraph_get()
obj_eval = obj.evaluated_get(depsgraph)
mesh = obj_eval.to_mesh()
```

提取完成后必须：

```python
obj_eval.to_mesh_clear()
```

支持：

- Modifier。
- Blender 最终可见 geometry。
- material slot。
- split normals。
- UV0。

MVP 只同步三角形。

建议：

```text
mesh.calc_loop_triangles()
mesh.calc_tangents(...)
```

---

## 第一版 Vertex Strategy

MVP 不先写复杂 vertex dedup。

按 triangle loop flatten：

```text
每个 triangle corner
    ↓
position
normal
tangent
uv0
```

优点：

- UV seam 正确。
- split normal 正确。
- tangent 正确。
- material boundary 简单。
- 实现和调试简单。

代价：

- vertex 数量增大。
- 网络包更大。

Live Link 的第一目标是正确性。

后续再做 tuple dedup：

```text
(position index, normal, tangent, uv)
```

---

## Mesh Binary Payload

概念布局：

```text
MeshHeader
positions[vertex_count]
normals[vertex_count]
tangents[vertex_count]
uv0[vertex_count]

for submesh:
    material_slot
    index_count
    indices[index_count]
```

至少包含：

```text
object_guid
revision
vertex_count
submesh_count
flags
bounds
```

Ailu：

```text
Create Mesh
SetVertices
SetNormals
SetTangents
SetUVs
AddSubmesh
SetBounds
Apply
```

然后绑定：

```text
StaticMeshComponent::_p_mesh
```

Live Link Mesh cache 用：

```text
object guid
```

作为 MVP key。

原因：

同一个 Blender Mesh datablock 在不同 Object 上经过 Modifier 后，evaluated geometry 可能不同。

共享 Mesh / Instancing 是后续优化。

---

# Material Strategy

MVP 只认：

```text
Principled BSDF
```

如果 Material 没有受支持的 Principled：

```text
fallback default material
log warning once per material revision
```

不要自动翻译任意节点图。

---

## 参数映射

```text
Blender                           Ailu

Base Color              ->       _AlbedoValue
Base Color Image        ->       _AlbedoTex

Roughness               ->       _RoughnessValue
Metallic                ->       _MetallicValue

Normal Map Image        ->       _NormalTex

Emission Color/Strength ->       _EmissionValue
Emission Image          ->       _EmissionTex

IOR                      ->       _IOR
Transmission             ->       _Transmission
```

第一阶段材质：

```text
Base Color factor
Roughness scalar
Metallic scalar
Emission factor
Base Color texture
Normal texture
```

第二阶段再增加：

```text
Roughness/Metallic texture packing
IOR
Transmission
```

---

## Roughness / Metallic Texture

Ailu 标准材质使用：

```text
_RoughnessMetallicTex

R = Roughness
G = Metallic
```

Blender 通常可以来自不同 Image/Channel。

因此 Blender addon 负责生成 cached packed texture：

```text
R = roughness
G = metallic
B = 0
A = 1
```

不要在 Ailu 端每次 material update 做 CPU texture repack。

---

# Texture Strategy

## Regular Image

Blender 发送：

```text
image_guid
revision
absolute_path
usage
```

Ailu 主线程：

```cpp
ResourceMgr::LoadExternalTexture(...)
```

Live Link cache 持有：

```cpp
Ref<Texture2D>
```

不创建 Project Asset。

---

## Packed / Generated Image

Blender addon 导出到：

```text
<temp or addon cache>/AiluLiveLink/<session_guid>/
```

例如：

```text
image_guid_revision.png
```

然后发送路径。

断开后可以延迟清理 cache。

---

## Color Space

至少按 usage 明确：

```text
BaseColor / Emission -> sRGB
Normal / RM          -> Linear
```

不要单纯相信 Blender Image color-space name 映射到引擎内部枚举。

---

# Light Strategy

支持：

```text
SUN
POINT
SPOT
AREA RECTANGLE/SQUARE
```

映射：

```text
SUN   -> kDirectional
POINT -> kPoint
SPOT  -> kSpot
AREA  -> kArea
```

第一版 Area：

```text
RECTANGLE
SQUARE
```

Blender：

```text
DISC
ELLIPSE
```

暂时近似或 warning，不声称完全一致。

---

## Intensity

Blender `energy` 与 Ailu 当前 `_light_color.a` / renderer intensity 语义并非直接统一物理单位。

所以 addon 提供：

```text
Light Intensity Scale
```

默认值通过 golden scene 实测设定。

不要在 MVP 文档中承诺：

```text
Blender 1000 W == Ailu 某个严格物理值
```

---

# Camera Strategy

Blender 发送：

```text
projection
world_position
world_forward
world_up
horizontal_fov
near
far
ortho_scale
aspect
```

Ailu：

```text
Position
Quaternion::LookRotation
FovH
Near
Far
Size
Aspect
```

注意：

```text
Blender radians
Ailu FovH degrees
```

M4 必须验证：

- 36mm/50mm 等典型 lens。
- 横向 FOV。
- portrait/landscape aspect。
- ortho framing。

---

# Full Sync Semantics

连接成功后：

```text
HELLO
    ↓
HELLO_ACK
    ↓
FULL_SYNC_BEGIN(session_guid, revision)
    ↓
Objects
Meshes
Materials
Textures
Lights
Cameras
    ↓
FULL_SYNC_END
```

Ailu session：

```text
FULL_SYNC_BEGIN
    ↓
标记 existing session objects unseen

本次收到
    ↓
mark seen

FULL_SYNC_END
    ↓
删除仍 unseen 的 stale object/resource mapping
```

这样 reconnect 不需要依赖 Blender 精确发送所有离线期间 delete event。

---

# Incremental Sync

使用：

```python
bpy.app.handlers.depsgraph_update_post
```

根据更新类型标 dirty：

```text
transform
geometry
shading
```

handler 不立即发 socket。

统一 debounce：

```text
~50 ms
```

transform 可以更高频：

```text
16~33 ms
```

geometry/material：

```text
50~150 ms
```

最终参数以 profiling 为准。

---

## Coalescing

同一对象：

```text
Transform v1
Transform v2
Transform v3
```

Ailu Tick 前只需要：

```text
Transform v3
```

Mesh：

```text
revision 17
revision 18
```

未开始 apply 17 时，允许丢 17。

不要积累拖 gizmo 时的全部历史消息。

---

# Resource Ownership

插件拥有：

```text
socket
worker
command queue
session state
Blender guid -> Ailu entity mapping
transient Mesh refs
transient Material refs
transient Texture refs
```

Scene 拥有：

```text
Entity
StaticMeshComponent
LightComponent
CCamera
TransformComponent
```

但 Live Link Entity 标 transient。

插件断开/卸载时：

```text
remove transient root
clear mapping
release cache refs
```

不要删除用户 Project Asset。

---

# Milestones

详细内容见：

```text
milestones/m0-plugin-transport.md
milestones/m1-session-transform.md
milestones/m2-static-mesh.md
milestones/m3-material-texture.md
milestones/m4-light-camera.md
milestones/m5-incremental-reconnect.md
milestones/m6-stability-package.md
```

顺序严格：

```text
M0
 ↓
M1
 ↓
M2
 ↓
M3
 ↓
M4
 ↓
M5
 ↓
M6
```

一个里程碑 Validation 未通过，不进入下一个。

---

# Current Milestone

```text
M6 implementation complete; runtime acceptance pending
```

M0–M6 source implementation and Debug build are complete. Editor transport and Blender scene/visual
acceptance remain pending; see `progress.md` for the runtime environment blocker and exact evidence levels.

M0 的任务不是“同步 Cube”，而是验证：

```text
existing Plugin System
        +
BlenderLiveLink DLL
        +
TCP worker
        +
Blender test client
```

生命周期安全。

---

# Progress

当前：

```text
[done] 检查最新源码插件系统
[done] 检查 Editor Plugin lifecycle
[done] 检查 Scene / ECS identity API
[done] 检查 Mesh direct construction API
[done] 检查 Material standard PBR parameters
[done] 检查 Texture external load path
[done] 检查 Light / Camera representation
[done] 确定 Mesh custom binary strategy
[done] 确定 transient session requirement
[in progress] M0 plugin scaffold and loopback transport implemented; Debug build passed, runtime acceptance pending
```

实时进度维护在：

```text
progress.md
```

---

# Decision Log

## D001 — 不重新设计插件系统

当前插件系统已足够承载 Live Link。

决定：

```text
Plugin API Version 保持 1
```

---

## D002 — Mesh 使用自定义 Binary Payload

不使用：

```text
Blender Export FBX
Blender Export GLB
```

作为 Live Link 核心路径。

---

## D003 — Texture 使用本机路径

MVP 只支持同机 Blender ↔ Ailu。

普通 Image：

```text
absolute path
```

Packed/Generated：

```text
addon temp export
```

---

## D004 — Material 只支持 Principled 子集

不实现任意 Blender node graph compiler。

---

## D005 — Live Link Scene Entity 必须 transient

不能污染 scene save。

---

## D006 — Blender Object UUID 直接成为 Ailu Persistent Entity Guid

不使用名字进行匹配。

---

## D007 — Plugin `OnLoad()` 不访问 Scene/Graphics

因为当前 Editor 初始化顺序中，autoloader 早于 Scene Open。

---

## D008 — 插件所有 Engine 修改发生在 Tick 主线程

network worker 永远不碰 ECS/Render resource。

---

## D009 — Full Sync 是 reconnect 的 authoritative recovery

Incremental event 只负责低延迟。

正确性由 Full Sync 兜底。

---

# Surprises & Discoveries

## S001 — 插件系统已经完整落地

最新源码已经包含原插件 ExecPlan 的关键阶段：

```text
DynamicLibrary
PluginManager
Manifest
RegistrationScope
Editor integration
```

Blend Link 不需要先完成另一个大型基础重构。

---

## S002 — Plugin OnLoad 比 Scene Open 更早

这是实现 Live Link 时最容易踩的生命周期坑。

不能在 `OnLoad()` 创建 Scene entity。

---

## S003 — 当前 RegistrationScope 还不是通用 Editor UI Extension 系统

因此 MVP 不依赖 Ailu Editor 插件面板。

连接 UI 放 Blender Add-on。

Ailu 侧先用日志/状态即可。

---

## S004 — 当前 `ImportResource` 不适合作为 Live Link Texture 路径

源码检查发现当前 `ResourceMgr::ImportResourceImpl` 的返回对象路径并不适合作为 Live Link transient
resource API，而且 ImportResource 本身会产生 Project Asset 语义。

决定使用：

```cpp
ResourceMgr::LoadExternalTexture(...)
```

---

## S005 — 当前 glTF parser 不适合作为 Live Link Mesh/Material 主路径

当前实现对实时 Live Link 并没有足够优势，而且材质映射不是完整 Principled。

直接构造 `Render::Mesh` 更短、更可控。

---

## S006 — Area Light 类型比当前 runtime 实现更丰富

Blender Area Light 支持更多 shape，但 Ailu 当前实际 corners path 主要是 rectangle semantics。

MVP 明确限制。

---

## S007 — Live Link object 默认会被 Scene save

当前 scene serialization 没有 Live Link transient 过滤。

M1 必须补这个 generic engine capability。

---

# Validation

全局完成标准：

```text
1. Ailu Editor autoload Live Link plugin
2. Blender Connect
3. Full Sync
4. Static Mesh 正确
5. Material/Texture 基础一致
6. Light/Camera 朝向正确
7. Transform 增量同步
8. Mesh 修改增量同步
9. Material 修改增量同步
10. 删除/重命名/父子关系正确
11. Disconnect/Reconnect 不留下 stale object
12. Save Scene 不写入 transient Live Link entities
13. Play Mode 不被 Live Link 修改
14. Plugin unload with active connection 不 crash/hang
15. 连续 connect/disconnect stress 通过
```

性能不是第一验收项，但 M6 至少记录：

```text
transform end-to-end latency
100k triangle mesh update latency
texture reload latency
1000 object full sync duration
```

---

# Known Follow-ups

MVP 后再评估：

- Mesh vertex tuple dedup。
- Mesh compression。
- Shared Mesh datablock / instancing。
- Collection mapping。
- Blender World/HDRI。
- Alpha blend / mask material。
- Clearcoat。
- Subsurface。
- Volume。
- Better photometric light mapping。
- Remote blob transport。
- Editor-side Plugin UI。
- Per-plugin service interfaces。
- Live Link recording。
- Ailu → Blender selected object sync。
- Runtime Player Live Link。

---

# Outcomes

完成 M6 后，AiluEngine 将拥有：

1. 第一个真实的动态 Editor Plugin 使用案例。
2. Blender 场景实时桥接。
3. 不污染 Project Asset 的 transient DCC preview path。
4. 可复用的 local Live Link transport。
5. Blender → Ailu 的稳定 Object identity。
6. 可继续扩展动画/骨骼/Prefab 的基础。

更重要的是：

> Live Link 不会侵入 AiluEngine 的核心 Scene/Asset/Render 架构；除 transient entity 这类通用能力外，
> 大部分实现都留在插件与 Blender Add-on 中。

---

# Resume Protocol

任何模型继续该任务前：

1. 先读本文件。
2. 再读 `progress.md`。
3. 只打开 `progress.md` 指向的 Current Milestone 文件。
4. 检查该 milestone 的 Validation。
5. 不要提前实现后续 milestone。
6. 如果源码状态与 Baseline 不一致：
   - 更新 `progress.md` 的 `Surprises & Discoveries`。
   - 记录新的 Decision。
   - 再继续。
7. 每次完成一个可验证步骤：
   - 更新 milestone checklist。
   - 更新 `progress.md`。
   - 写实际执行过的 Validation 命令和结果。
8. 不要仅写“应该通过”。
9. 只有当前 milestone 全部验收通过，才修改：
   - `Current Milestone`
   - milestone 状态
   - Next Action
10. 如果任务因上下文压缩中断，从 `progress.md` 恢复，不重新扫描整个仓库。
