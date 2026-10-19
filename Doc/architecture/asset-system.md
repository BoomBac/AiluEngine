# Asset System Architecture

## Purpose

本文档描述 AiluEngine 当前 asset/resource 系统中几个容易混淆的层次。

主要入口：

- `Engine/Inc/Framework/Common/ResourceMgr.h`
- `Engine/Inc/Assets/Asset.h`
- `Engine/Inc/Assets/AssetRegistry.h`
- `Engine/Inc/Assets/AssetDocument.h`
- `Engine/Inc/Assets/AssetArtifact.h`
- `Engine/Inc/Assets/DerivedDataCache.h`
- `Engine/Inc/Assets/AssetHandlers.h`

## Conceptual layers

当前可以按以下概念理解：

```text
Source / Asset Document
        ↓
Importer / Handler
        ↓
Artifact / Derived Data Cache
        ↓
Runtime Object
        ↓
AssetRegistry slot / revision / handle
        ↓
Consumers
```

`ResourceMgr` 横跨这些层次，当前作为 orchestration facade 存在。

## Asset document

Asset document 是可持久化的资产描述。

典型内容包括：

- header / guid；
- import or editor data；
- Scene document；
- component document；
- graph document；
- probe data 等。

Document 是长期格式的一部分。

修改 document 时必须考虑：

- version；
- backward compatibility；
- migration；
- reflection/generated serialization。

## Artifact

Artifact 是 import/derived 结果。

`AssetArtifactKey` 当前包含：

```text
source hash
import setting hash
dependency hash
importer version
artifact version
```

Artifact container 具有：

```text
magic
container version
type
artifact version
sections
cache key
```

因此 importer 行为或二进制布局变化时，应考虑 importer/artifact version，而不是仅删除缓存解决。

## AssetRegistry

`AssetRegistry` 当前提供 runtime stable slot/handle 语义。

核心概念：

```text
AssetHandle
AssetSlot
slot generation
revision
load/update state
dependencies
dependents
Publish
```

消费者应优先通过稳定 handle/revision 感知对象替换，而不是长期缓存任意 raw pointer 并假设资产永远不变。

`Publish` 表达“新的 runtime object 成为该资产当前版本”。

## ResourceMgr

`ResourceMgr` 当前承担较多职责，包括：

- asset mount/domain/path；
- asset database；
- create/delete/rename/move/save；
- import/reimport；
- reload；
- file watcher integration；
- sub-assets；
- async/sync tasks；
- runtime resource lookup/loading；
- access to AssetRegistry。

这是当前事实，不代表新增资产逻辑都应该继续加入 `ResourceMgr`。

新增功能优先问：

```text
Is this path/mount orchestration?
Is this importer/handler logic?
Is this registry state?
Is this DDC/artifact logic?
Is this editor-only workflow?
```

再选择位置。

## Synchronous import result

`ResourceMgr::ImportResource()` runs synchronously and returns the first runtime object for which asset creation succeeded.
A null result means no registered output (including unsupported/missing input, an invalid destination, duplicate input, or importer failure).
This API does not distinguish those null-result causes or expose partial sub-asset failure details. Editor batch progress uses this
result for file-level success/failure; it does not infer success merely from returning normally. Single-file import may still stall the
main thread; Editor queue preparation/yielding only gives drawing opportunities between files.

## Asset domains and paths

当前可见 domain/path scheme 包括：

```text
engine://
editor://
project://
```

路径转换应通过现有 ResourceMgr/path API。

不要在 subsystem 中重复拼接 engine/editor/project system paths。

## Reload model

File watcher 不应直接在 watcher callback 中任意替换 runtime state。

现有设计已经体现 queue/mark/apply 的方向：

```text
watcher detects change
→ mark/enqueue reload
→ ResourceMgr Tick / synchronization point
→ load candidate
→ Publish
```

新增 hot reload 应保持这种“异步发现 + 稳定点发布”的思路。

## Revision-aware consumers

缓存 asset-derived 数据时，应记录：

```text
AssetHandle
last seen revision
derived cache
```

发现 revision 改变后再 rebuild。

避免：

```text
every frame rebuild
```

也避免：

```text
cache raw pointer forever
```

## Dependencies

`AssetRegistry` 存储 dependency/dependent 信息。

修改 importer、shader include、material/texture、mesh dependency 等系统时，应尽量让依赖关系显式，而不是通过“全量 reload”解决。

## Sub-assets

ResourceMgr 支持 sub-asset register/unregister。

任何 sub-asset identity 改动都需要检查 owner asset GUID 和 reload 后 identity 稳定性。

## Shutdown

Resource/asset subsystem shutdown 时要处理：

```text
stop new async work
→ drain/cancel import/load tasks
→ stop watcher callbacks
→ release runtime resources
→ clear registry
```

如果未来 plugin 可以注册 importer/handler，卸载 plugin 前还必须先移除其 registry callbacks/handlers 和 outstanding jobs。

## Current pressure point

长期方向可以逐步减少 ResourceMgr 的“万能入口”角色，但这不是普通任务的默认重构目标。

普通任务应：

- 在正确 layer 添加最小改动；
- 不额外扩张 ResourceMgr；
- 不为了追求理论纯度大范围拆分已有系统。

## Change checklist

修改资产系统时检查：

- GUID identity；
- document format；
- artifact version；
- importer version；
- DDC invalidation；
- AssetRegistry revision；
- dependencies；
- hot reload；
- sub-assets；
- Editor browser/import UI；
- runtime pointer lifetime；
- async shutdown。

## Durable procedural mesh sources

MeshAssetHandler saves a non-skeletal mesh with no external model source as a .almesh sidecar beside its .alasset document.
The sidecar uses the existing versioned MeshArtifact container and a native key with kMeshArtifactVersion; it is durable
source data, not disposable DDC. Load recognizes the .almesh source, restores CPU/derived geometry and uploads it through
the normal Mesh path. Existing FBX/glTF import and DDC paths remain unchanged. Skeleton meshes still require their existing
source pipeline. Keep the sidecar with the document when moving or distributing these assets.

## HDR environment sources

TextureImporter decodes EXR through the existing TinyEXR implementation into an RGBA32F ScratchImage before its normal
resize/mipmap/artifact path. Scene-linear World sources use `kHdr`, linear sampling and no compression, preserving values
above one across save/reload. Their EXR sidecars are durable source files beside the texture asset documents.
The 2D mip-size helper clamps each dimension to one, including the final mips of rectangular HDR textures.
