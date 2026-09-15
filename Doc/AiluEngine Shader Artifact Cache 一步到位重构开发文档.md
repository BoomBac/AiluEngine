# AiluEngine Shader Artifact Cache 一步到位重构

## 1. 背景

AiluEngine 当前已经存在 DXC Shader Cache，但实际启动时仍存在明显的 Shader 加载开销，典型日志：

```text
Shader(color_picker_sv_0_0,keywords: ) compile success with 65.7136ms
Shader(defered_standard_lit_0_0,keywords: _) compile success with 221.667ms
Shader(texture_sky_0_0,keywords: ) compile success with 60.8712ms
Shader(ssao_0_0,keywords: ) compile success with 90.8253ms
Shader(standard_volume_0_0,keywords: ) compile success with 91.3109ms
Shader(defered_standard_lit_0_1201166180,keywords: ALPHA_TEST) compile success with 183.872ms
```

目前 cache 实际主要缓存：

```text
DXIL
Reflection Blob
PDB
```

但 Shader Variant 初始化仍然会重复执行：

```text
ParseIncludeDependencies
    ↓
递归打开 HLSL / include
    ↓
解析 #include
    ↓
filesystem timestamp 检查
    ↓
加载 DXIL
    ↓
创建 DXC Reflection
    ↓
LoadShaderReflection
    ↓
LoadAdditionalShaderReflection
    ↓
扫描 HLSL 文本
    ↓
GenerateInternalPSO
    ↓
Serialize RootSignature
    ↓
CreateRootSignature
    ↓
BuildBindingLayout
```

因此当前的 cache 本质上只是：

> Compiler Output Cache

而目标应升级为：

> Shader Runtime Artifact Cache

即：

**cache hit 后能够直接恢复运行所需要的大部分 Shader Runtime State。**

本次重构同时覆盖：

- Graphics Shader
  - Vertex Shader
  - Pixel Shader
  - Geometry Shader
  - Pass
  - Variant
- Compute Shader
  - Kernel
  - Variant
- Shader dependency
- Reflection metadata
- Binding layout
- Root Signature
- DXC compiler context
- Hot Reload
- Artifact invalidation
- Debug / Development / Release 编译模式

不保留现有 `.dxil + .rft` cache 作为主流程。

---

# 2. 当前主要问题

## 2.1 Cache Hit 仍然扫描完整 Include Tree

当前：

```cpp
IsShaderCacheCurrent(...)
```

内部重新执行：

```text
ParseIncludeDependencies(source)
```

即每次 cache lookup 都递归读取：

```text
shader.hlsl
    Common.hlsl
        ShaderInterop.h
    Lighting.hlsl
        PBR.hlsl
        Shadow.hlsl
```

每个文件都需要：

```text
Exist
ReadFile
ParseIncludeStatements
ResolveInclude
last_write_time
```

而 Graphics Shader 的 VS / PS 又分别执行一次。

例如：

```text
defered_standard_lit

Variant 0
    VS dependency scan
    PS dependency scan

Variant ALPHA_TEST
    VS dependency scan
    PS dependency scan

Pass 1
    VS dependency scan
    PS dependency scan
```

大量工作完全重复。

---

## 2.2 Cache Hit 后重新 Reflection

当前 DXC cache hit 后仍然加载：

```text
.rft
```

随后：

```cpp
CreateReflection(...)
```

然后再经过：

```cpp
LoadShaderReflection(...)
```

重新生成：

```text
VertexInputLayout
ShaderBindResourceInfo
CBuffer information
Resource slots
```

这些内容在 Shader 未改变时全部都是稳定数据，不应每次启动重新生成。

---

## 2.3 LoadAdditionalShaderReflection 重复解析 HLSL

目前 Reflection 后还有：

```cpp
LoadAdditionalShaderReflection(...)
```

重新打开 Shader 文件并逐行分析：

```text
Texture
TextureCube
RWTexture
Buffer
CBUFFER
array
struct
...
```

这相当于 DXC Reflection 完成后又执行了一轮 CPU Shader Parser。

Artifact 应直接保存最终结果。

---

# 3. 重构目标

最终 warm cache Shader 加载路径：

```text
ShaderAsset
    ↓
PreProcessShader
    ↓
构造 ShaderArtifactKey
    ↓
ShaderArtifactCache::Load()
    ↓
ValidateArtifact()
    ↓
反序列化 ShaderVariantArtifact
    ↓
恢复 DXIL
    ↓
恢复 Reflection Metadata
    ↓
恢复 BindingLayout
    ↓
RootSignaturePool 获取 RootSignature
    ↓
Variant Ready
```

不得再执行：

```text
DXC Compile
ParseIncludeDependencies
CreateReflection
LoadShaderReflection
LoadAdditionalShaderReflection
D3DX12SerializeVersionedRootSignature
```

正常目标：

```text
简单 Shader Variant warm load:
    < 1~3 ms

复杂 Shader Variant warm load:
    < 3~5 ms
```

而不是当前：

```text
50~200+ ms
```

---

# 4. 总体架构

新增：

```text
ShaderArtifactCache
│
├── ShaderArtifactKey
│
├── ShaderArtifactHeader
│
├── ShaderArtifactDependency
│
├── GraphicsShaderVariantArtifact
│
├── ComputeShaderVariantArtifact
│
├── ShaderArtifactSerializer
│
└── ShaderDependencyRegistry
```

DX12 层新增：

```text
DxcCompilerContext
DxcCompilerContextPool / thread_local context

D3DRootSignaturePool
```

推荐文件：

```text
Engine/Inc/Render/ShaderArtifact.h
Engine/Src/Render/ShaderArtifact.cpp

Engine/Inc/Render/ShaderArtifactCache.h
Engine/Src/Render/ShaderArtifactCache.cpp

Engine/Inc/Render/ShaderDependencyRegistry.h
Engine/Src/Render/ShaderDependencyRegistry.cpp

Engine/Inc/RHI/DX12/DxcCompilerContext.h
Engine/Src/RHI/DX12/DxcCompilerContext.cpp

Engine/Inc/RHI/DX12/D3DRootSignaturePool.h
Engine/Src/RHI/DX12/D3DRootSignaturePool.cpp
```

现有主要修改：

```text
Render/Shader.cpp
Render/Shader.h

RHI/DX12/D3DShader.cpp
RHI/DX12/D3DShader.h

RHI/DX12/D3DShaderCompiler.cpp
RHI/DX12/D3DShaderCompiler.h

Assets/AssetHandlers.cpp
```

---

# 5. Artifact 粒度设计

不要继续以单个 Stage：

```text
VS.dxil
PS.dxil
```

作为主要 cache 粒度。

Graphics Shader 应以：

```text
Pass + Variant
```

作为一个 Artifact。

即：

```text
GraphicsShaderVariantArtifact
    VS
    PS
    GS
    Runtime Metadata
```

原因是这些数据本来就需要一起才能构建一个可用 Variant。

Compute Shader 使用：

```text
Kernel + Variant
```

作为 Artifact。

---

# 6. ShaderArtifactKey

Artifact Key 必须是稳定的跨进程 Key。

禁止：

```cpp
std::hash<String>{}(key);
```

作为磁盘持久 cache 唯一依据。

定义：

```cpp
enum class EShaderArtifactType : u8
{
    kGraphics,
    kCompute
};

struct ShaderArtifactKey
{
    EShaderArtifactType _type = EShaderArtifactType::kGraphics;

    String _source_path;

    String _pass_or_kernel_name;

    u16 _pass_or_kernel_index = 0u;

    ShaderVariantHash _variant_hash = 0u;

    String _backend;

    u32 _shader_artifact_version = 0u;
    u32 _shader_abi_version = 0u;
    u32 _compiler_version = 0u;

    u64 _compile_config_hash = 0u;
};
```

最终：

```cpp
u64 ShaderArtifactKey::Hash() const;
```

必须使用确定性算法，例如：

```text
FNV1a64
xxHash64
现有稳定 ALHash 实现
```

前提：

**同样输入跨进程结果完全一致。**

---

# 7. Compile Config Hash

需要把所有可能改变 DXIL 或 Shader ABI 的参数包含进去。

至少包含：

```text
backend
shader model

DXC version

compile mode

-Zi
-Od
-O3
-Qembed_debug

_REVERSED_Z
SHADER_DXC

AL_PACKAGE_SHADER_INTEROP
AL_SHADER_INTEROP_CBUFFER_AS_STRUCT

所有 global macros

所有 variant macros:
    name
    definition
```

注意当前只使用 macro name 生成 cache key 的逻辑需要移除。

例如：

```text
LIGHT_COUNT=4
```

与：

```text
LIGHT_COUNT=8
```

必须生成不同 key。

所有 macro 进入 hash 前：

```text
按 name 排序
```

防止容器迭代顺序影响 cache。

---

# 8. Artifact Header

```cpp
struct ShaderArtifactHeader
{
    static constexpr u32 kMagic = 0x41485341u;
    static constexpr u32 kVersion = 1u;

    u32 _magic = kMagic;
    u32 _version = kVersion;

    EShaderArtifactType _type = EShaderArtifactType::kGraphics;

    u64 _artifact_key = 0u;

    u64 _source_fingerprint = 0u;

    u32 _dependency_count = 0u;

    u32 _payload_size = 0u;
};
```

`kMagic` 对应：

```text
ASHA
```

Artifact 文件建议：

```text
cache/
    shader_artifacts/
        dx12/
            ab/
                abcdef1234567890.ashader
```

不要所有文件平铺到同一目录。

---

# 9. Dependency Metadata

编译 Shader 时已经能够通过 DXC Include Handler 获取实际访问的 include：

```cpp
std::set<WString> _include_files;
```

利用这个信息。

定义：

```cpp
struct ShaderArtifactDependency
{
    WString _path;

    u64 _file_size = 0u;

    u64 _last_write_time = 0u;

    u64 _content_hash = 0u;
};
```

Artifact 保存：

```text
source shader
+
实际参与当前 variant 编译的 include
```

例如：

```text
defered_standard_lit.hlsl
Common.hlsl
ShaderInterop.h
PBR.hlsl
Lighting.hlsl
Shadow.hlsl
```

---

# 10. Dependency 校验

禁止在 Cache Hit 判断阶段调用：

```cpp
ParseIncludeDependencies(...)
```

新的：

```cpp
bool ShaderArtifactCache::ValidateDependencies(const ShaderArtifact &artifact);
```

只检查 artifact 已保存的 dependency list。

Fast Path：

```text
file exists
file size same
timestamp same
    ↓
valid
```

Slow Path：

如果：

```text
size changed
或
timestamp changed
```

再计算：

```text
content hash
```

如果 hash 相同，则仍然可以视为有效，并更新 metadata。

这样避免：

```text
每次 Editor 启动读取所有 HLSL 全文
```

---

# 11. 同一启动周期内的 File Fingerprint Cache

新增：

```cpp
class ShaderFileFingerprintCache
{
public:
    static ShaderFileFingerprintCache &Get();

    const ShaderFileFingerprint &GetFingerprint(const WString &path);

    void Invalidate(const WString &path);

private:
    std::mutex _mutex;

    HashMap<WString, ShaderFileFingerprint> _fingerprints;
};
```

同一个：

```text
ShaderInterop.h
Common.hlsl
Lighting.hlsl
```

即使被 100 个 Variant 使用，本次启动只执行一次 filesystem 查询。

---

# 12. GraphicsShaderVariantArtifact

```cpp
struct ShaderStageArtifact
{
    Vector<u8> _byte_code;
};

struct ShaderResourceBindingArtifact
{
    String _name;

    ShaderPropertyId _property_id = kInvalidShaderPropertyId;

    EBindResDescType _resource_type = EBindResDescType::kNone;

    u16 _resource_slot = 0u;
    u16 _bind_slot = 0u;
    u16 _register_space = 0u;

    u32 _buffer_offset = 0u;
    u32 _buffer_size = 0u;

    u16 _array_size = 0u;
};

struct GraphicsShaderVariantArtifact
{
    ShaderArtifactHeader _header;

    ShaderStageArtifact _vertex_shader;
    ShaderStageArtifact _pixel_shader;
    ShaderStageArtifact _geometry_shader;

    Vector<VertexBufferLayoutDesc> _vertex_input_layout;

    Vector<ShaderResourceBindingArtifact> _bindings;

    i8 _per_material_slot = -1;
    i8 _per_camera_slot = -1;
    i8 _per_scene_slot = -1;

    Vector<u8> _root_signature_blob;

    u64 _root_signature_hash = 0u;

    Vector<ShaderArtifactDependency> _dependencies;
};
```

不要直接序列化：

```text
ID3DBlob*
ID3D12ShaderReflection*
ID3D12RootSignature*
Ref<>
pointer
```

只保存纯数据。

---

# 13. Graphics Shader Cache Miss 流程

新的 miss path：

```text
D3DShader::CompileVariant
    ↓
ShaderArtifactCache::Load
    ↓
MISS
    ↓
CompileGraphicsShaderArtifact
    ↓
DXC compile VS
DXC compile PS
DXC compile GS
    ↓
合并 include dependency
    ↓
DXC reflection
    ↓
构建 VertexInputLayout
    ↓
构建 ShaderResourceBindingArtifact
    ↓
执行必要的 Additional Reflection Parser
    ↓
构建最终 BindingLayout 描述
    ↓
构建 RootSignature desc
    ↓
Serialize RootSignature
    ↓
GraphicsShaderVariantArtifact
    ↓
ShaderArtifactCache::Store
    ↓
Hydrate runtime variant
```

只有这里允许：

```text
Reflection
HLSL Parser
Dependency Discovery
RootSignature Serialization
```

---

# 14. Graphics Shader Cache Hit 流程

```text
D3DShader::CompileVariant
    ↓
ShaderArtifactCache::Load
    ↓
HIT
    ↓
Validate
    ↓
HydrateGraphicsVariant
```

`HydrateGraphicsVariant()`：

```cpp
bool D3DShader::HydrateGraphicsVariant(u16 pass_index, ShaderVariantHash variant_hash,
                                      const GraphicsShaderVariantArtifact &artifact);
```

职责：

```text
创建 ID3DBlob / internal bytecode storage

恢复:
    VS
    PS
    GS

恢复:
    VertexInputLayout

恢复:
    ShaderBindResourceInfo

恢复:
    ShaderBindingLayout

获取:
    RootSignature

更新:
    pass variant runtime state
```

禁止调用：

```text
CreateReflection
LoadShaderReflection
LoadAdditionalShaderReflection
ParseIncludeDependencies
D3DX12SerializeVersionedRootSignature
```

---

# 15. Root Signature Pool

现在每个 Shader Variant 都：

```text
Serialize RootSignature
CreateRootSignature
```

很多 Shader 的 binding layout 实际完全一致。

新增：

```cpp
class D3DRootSignaturePool
{
public:
    static D3DRootSignaturePool &Get();

    ComPtr<ID3D12RootSignature> GetOrCreate(u64 root_signature_hash, const Vector<u8> &serialized_blob);

private:
    std::mutex _mutex;

    HashMap<u64, ComPtr<ID3D12RootSignature>> _root_signatures;
};
```

Artifact 保存：

```text
serialized root signature blob
root signature hash
```

Cache Hit：

```text
RootSignaturePool
    ↓
hash hit
    ↓
直接返回现有 ID3D12RootSignature
```

hash miss：

```text
CreateRootSignature(serialized blob)
```

仍然不需要：

```text
D3DX12SerializeVersionedRootSignature
```

---

# 16. 不缓存 Graphics PSO

不要把完整 Graphics PSO 放进 Shader Artifact。

Graphics PSO 还依赖：

```text
RenderTargetFormat
DepthFormat
BlendState
RasterState
DepthStencilState
VertexLayout
Topology
ShaderVariant
```

应该继续由：

```text
GraphicsPipelineStateMgr
```

管理。

Shader Artifact 只负责：

```text
Shader Runtime State
```

不要把两个系统耦合在一起。

---

# 17. ComputeShaderVariantArtifact

Compute Shader 必须使用同样架构。

定义：

```cpp
struct ComputeShaderVariantArtifact
{
    ShaderArtifactHeader _header;

    ShaderStageArtifact _compute_shader;

    Vector3UInt _thread_group_size = Vector3UInt::kOne;

    Vector<ShaderResourceBindingArtifact> _bindings;

    Vector<u8> _root_signature_blob;

    u64 _root_signature_hash = 0u;

    Vector<ShaderArtifactDependency> _dependencies;
};
```

Artifact 粒度：

```text
ComputeShader
    Kernel
        Variant
```

---

# 18. Compute Shader Cache Miss

新的：

```text
D3DComputeShader::Compile(kernel, variant)
    ↓
ShaderArtifactCache::Load
    ↓
MISS
    ↓
DXC Compile CS
    ↓
收集实际 include
    ↓
DXC Reflection
    ↓
获取:
    numthreads
    CBV
    SRV
    UAV
    register space
    buffer layout
    texture type
    bindless metadata
    ↓
Additional HLSL metadata parsing
    ↓
构建 Compute BindingLayout
    ↓
Serialize RootSignature
    ↓
生成 ComputeShaderVariantArtifact
    ↓
Store
    ↓
HydrateComputeVariant
```

---

# 19. Compute Shader Cache Hit

新增：

```cpp
bool D3DComputeShader::HydrateComputeVariant(u16 kernel_index, ShaderVariantHash variant_hash,
                                            const ComputeShaderVariantArtifact &artifact);
```

恢复：

```text
CS bytecode

_thread_num

_bind_res_infos

bind slot

resource type

register space

bindless metadata

RootSignature
```

随后创建：

```text
Compute Pipeline State
```

Compute PSO 创建本身一般很轻，第一版不需要放进 Shader Artifact。

后续如果性能数据证明 Compute PSO 创建明显，可以单独实现：

```text
D3D12 Pipeline Library
```

不要和本次 Shader Artifact Cache 混在一起。

---

# 20. ShaderBindingLayout Artifact 化

现在：

```cpp
BuildBindingLayout(...)
```

每次从 Reflection Info 再构造。

Artifact 应直接持久化构建 BindingLayout 所需要的数据。

推荐增加：

```cpp
struct ShaderBindingLayoutArtifact
{
    Vector<ShaderResourceBindingArtifact> _bindings;
};
```

Runtime：

```cpp
Ref<const ShaderBindingLayout> BuildBindingLayout(const ShaderBindingLayoutArtifact &artifact);
```

这个步骤只执行：

```text
vector → runtime object
```

不再执行：

```text
string parser
reflection
slot discovery
```

---

# 21. Property ID 处理

不要直接持久化：

```text
ShaderPropertyId
```

作为唯一依据，除非能够保证其跨启动完全稳定。

推荐 artifact 保存：

```cpp
String _name;
```

Runtime hydrate：

```cpp
binding._property_id = ShaderPropertyRegistry::Get().Intern(artifact_binding._name);
```

这样不会依赖：

```text
ShaderPropertyRegistry
```

的注册顺序。

---

# 22. DXC Compiler Context 重构

当前每次 `CreateFromFileDXC()` 创建：

```text
IDxcUtils
IDxcCompiler3
```

改成：

```cpp
class DxcCompilerContext
{
public:
    DxcCompilerContext();

    IDxcUtils *Utils() const;
    IDxcCompiler3 *Compiler() const;

private:
    ComPtr<IDxcUtils> _utils;
    ComPtr<IDxcCompiler3> _compiler;
};
```

推荐：

```cpp
DxcCompilerContext &GetThreadDxcCompilerContext()
{
    thread_local DxcCompilerContext s_context;
    return s_context;
}
```

每个 JobWorker：

```text
首次编译创建一次 DXC Context
后续一直复用
```

---

# 23. 移除 Include Handler 全局 Mutex

当前类似：

```cpp
inline static std::mutex s_compile_lock;
```

必须删除。

每个 compile 有独立：

```text
DxcIncludeHandlerEx
```

因此不要因为多个 Shader 同时加载 include 而使用一个 global mutex。

目标：

```text
JobWorker_0
    DXC compile

JobWorker_1
    DXC compile

JobWorker_2
    DXC compile

JobWorker_3
    DXC compile
```

能够真正并行。

若内部数据确实需要保护：

```text
mutex 必须属于单个 handler/context
```

而不是所有 Shader 共享。

---

# 24. Single Flight Compile

还需要防止：

```text
两个线程同时请求同一个 artifact
```

例如：

```text
Worker 1:
    SSAO variant0 miss

Worker 2:
    SSAO variant0 miss
```

不应该编译两遍。

ShaderArtifactCache 增加：

```cpp
struct ShaderArtifactCompileState
{
    std::mutex _mutex;
    std::condition_variable _condition_variable;

    bool _is_compiling = false;
};
```

或者使用现有 Job dependency / future 系统。

逻辑：

```text
first request
    ↓
owns compile

second request
    ↓
wait / depend on existing compile job
```

最终只生成一次 Artifact。

---

# 25. Artifact 写入必须原子化

多线程下禁止直接：

```text
open final_path
truncate
write
```

使用：

```text
<hash>.tmp
    ↓
write complete
    ↓
flush / close
    ↓
rename
    ↓
<hash>.ashader
```

避免 Editor 异常退出产生半个 Artifact。

加载时：

```text
magic invalid
version invalid
payload invalid
```

直接视为：

```text
cache miss
```

不要 crash。

---

# 26. Hot Reload

增加：

```cpp
class ShaderDependencyRegistry
{
public:
    void Register(u64 artifact_key, const Vector<ShaderArtifactDependency> &dependencies);

    void Unregister(u64 artifact_key);

    Vector<u64> FindAffectedArtifacts(const WString &changed_file);

private:
    HashMap<WString, HashSet<u64>> _dependency_to_artifacts;
};
```

Artifact Load / Compile 成功后注册：

```text
ShaderInterop.h
    ↓
artifact A
artifact B
artifact C
```

FileWatcher：

```text
Lighting.hlsl changed
    ↓
ShaderDependencyRegistry
    ↓
查找 affected artifact
    ↓
Invalidate
    ↓
只重编 affected variants
```

不再需要：

```cpp
shader->Compile(false);
```

无条件重编整个 Shader 所有 Variant。

---

# 27. Hot Reload 必须保留旧 Variant

不要：

```text
旧 variant 删除
    ↓
compile new variant
    ↓
如果失败
    ↓
Shader unavailable
```

正确：

```text
Old Ready Variant
    │
    ├──继续被 RenderThread 使用
    │
    └──后台 Compile New Variant
             ↓
          success
             ↓
       atomic / safe replace
```

如果 compile fail：

```text
继续保留旧 Shader
记录 compile error
```

Editor 不应因为一次 HLSL 错误导致整个材质消失。

---

# 28. Artifact Invalidation Reason

定义：

```cpp
enum class EShaderArtifactInvalidReason : u8
{
    kNone,

    kNotFound,

    kInvalidHeader,

    kArtifactVersionChanged,

    kShaderAbiChanged,

    kCompilerChanged,

    kCompileConfigChanged,

    kSourceChanged,

    kDependencyChanged,

    kCorrupted
};
```

日志：

```text
ShaderArtifact MISS defered_standard_lit/pass0/ALPHA_TEST reason=DependencyChanged file=Lighting.hlsl
```

不要只打印：

```text
compile shader
```

否则以后很难定位 cache 为什么失效。

---

# 29. Shader Artifact Version

至少维护两个版本。

```cpp
inline constexpr u32 kShaderArtifactVersion = 1u;
inline constexpr u32 kShaderAbiVersion = 1u;
```

区别：

```text
kShaderArtifactVersion
    Artifact 文件结构改变

kShaderAbiVersion
    Runtime Shader ABI 改变
```

例如修改：

```text
Root Signature layout
PerMaterial CBuffer convention
PerCamera slot
Bindless convention
ShaderInterop
```

提高：

```text
kShaderAbiVersion
```

即可全部安全失效。

---

# 30. DXC Version

需要把 DXC 版本纳入 compile config。

最简单方案：

```cpp
inline constexpr u32 kDxcCompilerVersion = 1u;
```

升级：

```text
dxcompiler.dll
dxil.dll
```

时手动 +1。

不需要第一版就实现复杂 DLL hash。

---

# 31. Shader Compile Mode

移除：

```cpp
#define SHADER_DEBUG_SYMBOLS 1
```

这种全局硬编码。

定义：

```cpp
enum class EShaderCompileMode : u8
{
    kDevelopment,
    kDebug,
    kRelease
};
```

推荐：

### Development

```text
-O3
无 PDB
无 embedded debug
```

Editor 默认使用。

### Debug

```text
-Zi
-Od
-Qembed_debug
PDB
```

仅真正需要 Shader Debug 时使用。

### Release

```text
-O3
无 debug info
```

---

# 32. PDB 不属于 Shader Artifact 主 Payload

Debug mode 下可以：

```text
cache/
    shader_artifacts/
        pdb/
```

单独保存：

```text
<shader_hash>.pdb
```

Artifact 本身不依赖 PDB 才能加载。

普通 Editor 模式：

```text
不生成 PDB
```

---

# 33. PreProcessShader 暂时保留

本次不需要把：

```cpp
Shader::PreProcessShader()
```

完全 artifact 化。

它目前负责：

```text
Pass metadata
Entry point
keywords
render state
```

这部分通常远小于 DXC / Reflection / dependency scan。

本次先保留：

```text
读取主 Shader 文件
解析 pass/kernel metadata
```

但禁止后续 Variant Cache Hit 再重复解析完整 include tree。

如果重构完成后 profiling 证明：

```text
PreProcessShader
```

仍然明显，可以单独增加：

```text
ShaderSourceMetadataArtifact
```

不要本次顺手继续扩大范围。

---

# 34. Shader Runtime API 调整

建议统一：

```cpp
bool Shader::CompileVariant(u16 pass_index, ShaderVariantHash variant_hash, EShaderCompilePolicy policy);

bool ComputeShader::CompileVariant(u16 kernel_index, ShaderVariantHash variant_hash,
                                   EShaderCompilePolicy policy);
```

定义：

```cpp
enum class EShaderCompilePolicy : u8
{
    kUseArtifact,
    kForceCompile
};
```

替换含义不清晰的：

```cpp
bool is_load_cache;
```

调用：

```cpp
shader->CompileVariant(pass_index, variant_hash, EShaderCompilePolicy::kUseArtifact);
```

Hot Reload：

```cpp
shader->CompileVariant(pass_index, variant_hash, EShaderCompilePolicy::kForceCompile);
```

---

# 35. Compiler API 调整

现有：

```cpp
CreateFromFileDXC(...)
```

职责过多。

拆成：

```cpp
bool CompileDxcStage(const D3DShaderCompileDesc &desc, D3DShaderCompileOutput &output);
```

这里只负责：

```text
DXC Compile
DXC output
dependency capture
```

禁止：

```text
磁盘 cache lookup
磁盘 artifact write
runtime Shader 操作
```

Cache 属于：

```text
ShaderArtifactCache
```

Compiler 只负责编译。

---

# 36. Graphics 编译器封装

新增：

```cpp
bool CompileGraphicsShaderArtifact(const GraphicsShaderCompileRequest &request,
                                   GraphicsShaderVariantArtifact &artifact);
```

Request：

```cpp
struct GraphicsShaderCompileRequest
{
    WString _source_path;

    u16 _pass_index = 0u;

    ShaderVariantHash _variant_hash = 0u;

    String _vertex_entry;
    String _pixel_entry;
    String _geometry_entry;

    Vector<ShaderMacro> _macros;

    EShaderCompileMode _compile_mode = EShaderCompileMode::kDevelopment;
};
```

职责：

```text
Compile stages
Merge reflection
Build metadata
Build RootSignature blob
Build artifact
```

---

# 37. Compute 编译器封装

对应：

```cpp
bool CompileComputeShaderArtifact(const ComputeShaderCompileRequest &request,
                                  ComputeShaderVariantArtifact &artifact);
```

Request：

```cpp
struct ComputeShaderCompileRequest
{
    WString _source_path;

    u16 _kernel_index = 0u;

    ShaderVariantHash _variant_hash = 0u;

    String _entry_point;

    Vector<ShaderMacro> _macros;

    EShaderCompileMode _compile_mode = EShaderCompileMode::kDevelopment;
};
```

---

# 38. Artifact Cache API

最终接口尽量保持简单：

```cpp
class ShaderArtifactCache
{
public:
    static ShaderArtifactCache &Get();

    bool Load(const ShaderArtifactKey &key, GraphicsShaderVariantArtifact &artifact,
              EShaderArtifactInvalidReason *reason = nullptr);

    bool Load(const ShaderArtifactKey &key, ComputeShaderVariantArtifact &artifact,
              EShaderArtifactInvalidReason *reason = nullptr);

    bool Store(const ShaderArtifactKey &key, const GraphicsShaderVariantArtifact &artifact);

    bool Store(const ShaderArtifactKey &key, const ComputeShaderVariantArtifact &artifact);

    void Invalidate(const ShaderArtifactKey &key);

    void Clear();
};
```

不要做成复杂的 Asset System。

这个类只负责：

```text
Shader Derived Data
```

---

# 39. 内存 Cache

同一次 Editor 生命周期中，再增加一级 memory cache：

```text
Runtime Variant
    ↓
Memory Artifact Cache
    ↓
Disk Artifact Cache
    ↓
Compile
```

例如：

```cpp
HashMap<u64, Ref<const GraphicsShaderVariantArtifact>> _graphics_artifacts;
HashMap<u64, Ref<const ComputeShaderVariantArtifact>> _compute_artifacts;
```

避免同一 Artifact 重复反序列化。

但 Runtime Shader 完成 hydrate 后，如果 Artifact Payload 很大，可以释放：

```text
DXIL duplication
```

因此 Memory Artifact Cache 可以只缓存：

```text
validation metadata
```

第一版不强制长期保留完整 DXIL。

---

# 40. 与现有 AssetHandler 的关系

`ShaderAssetHandler`：

```text
读取 .alasset
    ↓
定位 HLSL
    ↓
Shader::Create
```

可以继续保留。

但：

```text
Shader::Create
```

内部不能再理解为：

```text
一定真实 compile
```

而应理解为：

```text
Create Runtime Shader
    ↓
artifact load / compile fallback
```

ComputeShaderAssetHandler 同理。

---

# 41. Raw HLSL Create 也必须支持 Artifact

AiluEngine 目前还有直接：

```cpp
ComputeShader::Create(ResourceMgr::GetResSysPath(...))
```

或：

```cpp
Shader::Create(...)
```

的路径。

Artifact Key 不允许依赖 Asset Guid。

必须支持：

```text
canonical source path
```

作为 source identity。

因此 `.alasset` 和 raw `.hlsl` 都可以使用同一 Shader Artifact Cache。

---

# 42. Job System

保留当前 Shader 并行加载设计。

推荐任务粒度：

```text
Graphics:
    一个 Pass + Variant = 一个 Job

Compute:
    一个 Kernel + Variant = 一个 Job
```

不要拆：

```text
VS job
PS job
reflection job
```

太细。

DXC 本身已经是重任务。

---

# 43. Job Dependency

如果 Shader Variant 加载后需要：

```text
Material
PSO
```

不要 busy wait。

使用现有 Job dependency：

```text
ShaderVariantJob
    ↓
MaterialInitJob
    ↓
PSORequest
```

Artifact hit 也视为同一个 ShaderVariantJob，只是快速完成。

---

# 44. 日志和 Profiling

必须替换现在模糊的：

```text
Shader(...) compile success with 90ms
```

改成：

```text
ShaderVariant READY
shader=ssao
pass=0
variant=0
source=artifact
total=1.24ms
validate=0.08ms
read=0.42ms
deserialize=0.21ms
hydrate=0.53ms
```

Cache miss：

```text
ShaderVariant READY
shader=defered_standard_lit
pass=0
variant=ALPHA_TEST
source=compile
reason=DependencyChanged
total=142.3ms
dxc=110.2ms
reflection=8.1ms
metadata=3.4ms
root_signature=0.8ms
artifact_write=4.6ms
hydrate=1.1ms
```

Compute 同样格式。

---

# 45. Debug Counter

增加：

```cpp
struct ShaderArtifactStats
{
    std::atomic<u64> _graphics_hit_count = 0u;
    std::atomic<u64> _graphics_miss_count = 0u;

    std::atomic<u64> _compute_hit_count = 0u;
    std::atomic<u64> _compute_miss_count = 0u;

    std::atomic<u64> _invalid_dependency_count = 0u;

    std::atomic<u64> _compiled_variant_count = 0u;

    std::atomic<u64> _artifact_bytes_read = 0u;
    std::atomic<u64> _artifact_bytes_written = 0u;
};
```

Editor 启动结束打印：

```text
Shader Artifact Cache:
Graphics: 52 hit / 2 miss
Compute: 18 hit / 0 miss
Hit rate: 97.2%
Compiled variants: 2
Artifact read: 7.8 MB
```

---

# 46. 删除旧 Cache

重构完成后删除：

```text
cache/shader_cache/dxc/*.dxil
cache/shader_cache/dxc/*.rft
cache/shader_cache/fxc/*.cso
```

以及：

```text
IsShaderCacheCurrent()
```

作为运行时 cache 判断的职责。

`ParseIncludeDependencies()` 可以保留为：

```text
debug / tool
```

但不能出现在：

```text
warm shader load
```

主路径。

---

# 47. 不要保留两套 Cache Pipeline

禁止形成：

```text
ShaderArtifactCache
    ↓ miss
旧 DXIL cache
    ↓ miss
DXC
```

这样只会让逻辑越来越复杂。

最终必须是：

```text
ShaderArtifactCache
    ↓
hit → hydrate

miss → DXC compile → artifact → hydrate
```

旧 DXIL Cache 完全移除。

---

# 48. Graphics 最终流程

```text
Shader::Create
      │
      ▼
PreProcessShader
      │
      ▼
Pass / Variant
      │
      ▼
BuildArtifactKey
      │
      ▼
ShaderArtifactCache
  │              │
 HIT            MISS
  │              │
  │        DxcCompilerContext
  │              │
  │        Compile VS/PS/GS
  │              │
  │         Reflection
  │              │
  │         Metadata
  │              │
  │       RootSignature Blob
  │              │
  │          Store Artifact
  │              │
  └──────────────┘
         │
         ▼
HydrateGraphicsVariant
         │
         ▼
D3DRootSignaturePool
         │
         ▼
Shader Ready
```

---

# 49. Compute 最终流程

```text
ComputeShader::Create
       │
       ▼
Parse Kernels
       │
       ▼
Kernel / Variant
       │
       ▼
BuildArtifactKey
       │
       ▼
ShaderArtifactCache
   │              │
  HIT            MISS
   │              │
   │        Compile CS
   │              │
   │        Reflection
   │              │
   │        numthreads
   │              │
   │        Binding Info
   │              │
   │      RootSignature Blob
   │              │
   │        Store Artifact
   │              │
   └──────────────┘
          │
          ▼
HydrateComputeVariant
          │
          ▼
RootSignaturePool
          │
          ▼
Create Compute PSO
          │
          ▼
Compute Shader Ready
```

---

# 50. 推荐实施顺序

## Phase 1：基础 Artifact 系统

实现：

```text
ShaderArtifactKey
ShaderArtifactHeader
dependency metadata
GraphicsShaderVariantArtifact
ComputeShaderVariantArtifact
ShaderArtifactSerializer
ShaderArtifactCache
```

先保证：

```text
load
store
validation
atomic write
```

稳定。

---

## Phase 2：DXC Compiler 解耦

移除：

```text
CreateFromFileDXC 内部磁盘 cache
```

只保留：

```text
CompileDxcStage
```

实现：

```text
thread_local DxcCompilerContext
```

删除 Include Handler static compile mutex。

---

## Phase 3：Graphics Shader

实现：

```text
CompileGraphicsShaderArtifact
HydrateGraphicsVariant
```

迁移：

```text
VS
PS
GS
Reflection
BindingLayout
RootSignature
```

Graphics Shader 不再读取旧 cache。

---

## Phase 4：Compute Shader

实现：

```text
CompileComputeShaderArtifact
HydrateComputeVariant
```

覆盖：

```text
Kernel
Variant
numthreads
binding
bindless
RootSignature
Compute PSO
```

---

## Phase 5：RootSignaturePool

所有 Graphics / Compute Shader 共用：

```text
D3DRootSignaturePool
```

确保：

```text
相同 RootSignature Layout
```

只创建一个 native RootSignature。

---

## Phase 6：Hot Reload Dependency Registry

实现：

```text
dependency → artifacts
```

修改文件时只 invalid affected artifacts。

---

## Phase 7：Profiling 和清理

删除：

```text
旧 DXIL cache
旧 Reflection cache
重复 dependency parser
cache-hit reflection path
```

增加详细 profiling。

---

# 51. 验收标准

## Correctness

必须满足：

```text
Cold compile 与 warm artifact load 渲染结果完全一致
```

覆盖：

```text
普通 Graphics Shader
Multi Pass Shader
Variant Shader
ALPHA_TEST
Geometry Shader
UI Shader
Sprite Shader
Deferred Shader
PostProcess Shader

Compute Shader
Multi Kernel Compute Shader
Compute Variant
SRV
UAV
StructuredBuffer
RWBuffer
Texture3D
RWTexture3D
Bindless resource
```

---

## Cache Invalidation

修改：

```text
主 HLSL
```

对应 Shader artifact 必须 miss。

修改：

```text
include HLSL
```

所有依赖该 include 的 artifact 必须 miss。

修改：

```text
无关 HLSL
```

其他 Shader artifact 不得 miss。

---

## Variant Isolation

修改只被：

```text
ALPHA_TEST
```

variant 使用的条件 include 时：

```text
ALPHA_TEST artifact invalid
```

其他没有实际 include 该文件的 variant：

```text
保持 valid
```

---

## Performance

Warm Editor Startup：

普通 Shader：

```text
单 Variant < 3 ms
```

大型 Shader：

```text
单 Variant < 5 ms
```

大量 Shader 加载时：

```text
不应出现 50~200 ms / Variant
```

除非：

```text
Artifact Miss
```

---

## Parallelism

4~8 个 JobWorker 同时 Shader Load 时：

```text
不得因为 DxcIncludeHandler global mutex 串行
```

CPU profiler 应能看到多个 Shader compile 并行执行。

---

# 52. 最终设计原则

本次重构需要遵守以下原则：

1. **Artifact Hit != Compile。**

   Hit 必须只是：

   ```text
   validate
   read
   deserialize
   hydrate
   ```

2. **Dependency Discovery 只允许出现在 Compile 阶段。**

3. **Reflection 只允许出现在 Compile 阶段。**

4. **HLSL Additional Parser 只允许出现在 Compile 阶段。**

5. **RootSignature Serialization 只允许出现在 Compile 阶段。**

6. **Graphics Shader 使用 Pass + Variant Artifact。**

7. **Compute Shader 使用 Kernel + Variant Artifact。**

8. **Artifact Cache 与 DXC Compiler 解耦。**

9. **Graphics PSO 不属于 Shader Artifact。**

10. **Compute PSO 第一版不持久化。**

11. **RootSignature 使用 Runtime Pool 去重。**

12. **不保留旧 DXIL Cache 作为第二级 Cache。**

最终整个系统应从：

```text
Shader Cache
≈
缓存 DXC 输出
```

升级为：

```text
Shader Artifact Cache
≈
缓存一个 Shader Variant 从源码编译到 Runtime Ready 之间所有可持久化结果
```

最终 warm path 应尽可能接近：

```text
Disk Artifact
    ↓
Runtime Shader Variant
```

而不是：

```text
Disk DXIL
    ↓
重新执行半套 Shader Compiler Pipeline
    ↓
Runtime Shader Variant
```

这才是本次 Shader Cache 重构完成的标准。