# AiluEngine Allocator Debug Panel 设计文档

> 用途：指导在 AiluEngine Editor 中实现一个面向自研内存分配器的实时调试面板。  
> 目标读者：负责实现编辑器工具、Allocator 统计接口和 ImGui 面板的开发者。  
> 参考界面：`AiluEngine_Allocator_Debug_Panel_Reference.png`

---

## 1. 背景

AiluEngine 当前的通用内存分配器采用以下结构：

```text
Allocator
└── Arena
    └── Bin
        ├── Partial Pages
        ├── Full Pages
        └── Empty Pages
            └── Page-local Free List
```

核心特征：

- 小对象按照 Size Class 映射到不同 Bin。
- 每个 Page 固定为 64 KB。
- Page 内部维护自己的 Free List。
- Bin 维护 Empty、Partial、Full 三类 Page 链表。
- 大对象回退到系统对齐分配。
- Debug 模式记录分配来源、大小、地址和调用位置。
- 当前可能存在 Thread-local Arena、跨线程释放和 Remote Free 的后续扩展需求。

随着分配器逐步投入引擎核心模块，需要一个专门的可视化面板，用于：

1. 判断分配器是否正确工作。
2. 定位内存泄漏、Double Free、Invalid Free 和越界。
3. 分析不同 Bin 的命中率与内部碎片。
4. 检查 Page 状态迁移是否正确。
5. 检查不同线程和 Arena 的分配压力。
6. 对比 Requested Bytes 与 Reserved Bytes。
7. 为后续优化 Thread Cache、Remote Free Queue 和 Page 回收策略提供数据。

---

# 2. 目标与非目标

## 2.1 目标

实现一个 Editor Dock Panel：

```text
Allocator Debug
```

支持：

- 实时查看全局分配器统计。
- 查看各 Size Class/Bin 状态。
- 查看单个 Page 的占用情况。
- 查看各线程/Arena 的分配情况。
- 查看实时 Allocate/Free 事件。
- 查看泄漏与内存错误。
- 暂停采样。
- 创建 Snapshot。
- 比较两个 Snapshot。
- 按线程、Tag、大小和来源过滤。
- 将统计结果导出为 JSON 或 CSV。
- 在 Debug 和 Profiling 构建中可启用。
- Release 构建中可以完全裁剪。

## 2.2 非目标

第一版不实现：

- 完整调用栈火焰图。
- 操作系统全部虚拟内存映射。
- GPU 显存统计。
- 第三方库内部堆分析。
- 无锁事件采集器。
- 类似 Visual Studio Diagnostic Tools 的完整 Heap Snapshot。
- 自动修复内存错误。
- 远程进程调试。

---

# 3. 总体架构

建议拆分为三个层级：

```text
Allocator Runtime
    ↓ 产生原始统计和事件
Memory Debug Service
    ↓ 采样、缓存、Snapshot、过滤
Allocator Debug Panel
    ↓ ImGui 展示与交互
```

对应模块：

```text
Engine/Runtime
  Allocator
  MemoryDebugService

Editor
  AllocatorDebugPanel
```

## 3.1 依赖方向

必须保持：

```text
Allocator
    ↓
MemoryDebugService
    ↓
Editor Panel
```

禁止：

```text
Allocator
    ↓
Editor
```

Allocator 不得直接依赖：

- ImGui；
- Editor；
- LogMgr；
- Scene；
- ResourceManager。

Allocator 错误输出应使用最低层级的调试输出，例如：

```cpp
std::fprintf(stderr, ...);
OutputDebugStringA(...);
```

避免日志系统反向依赖分配器。

---

# 4. 面板布局

建议面板整体采用四层布局：

```text
Toolbar
Summary Cards
Main Analysis Area
Event/Error Area
```

参考布局：

```text
┌──────────────────────────────────────────────────────────────────────┐
│ Toolbar                                                              │
├──────────────────────────────────────────────────────────────────────┤
│ Summary Cards                                                        │
├───────────────────────────────┬──────────────────────────────────────┤
│ Memory Trend                  │ Bin Statistics                       │
├───────────────────────────────┼──────────────────────────────────────┤
│ Page Detail                   │ Arena / Thread Statistics            │
├───────────────────────────────┼──────────────────────────────────────┤
│ Recent Allocation Events      │ Leaks and Errors                     │
└───────────────────────────────┴──────────────────────────────────────┘
```

---

# 5. Toolbar

## 5.1 控件

Toolbar 从左到右：

| 控件 | 说明 |
|---|---|
| Pause / Resume | 暂停或恢复 UI 数据采样 |
| Snapshot | 创建当前内存快照 |
| Diff | 比较两个快照 |
| Clear Events | 清空事件显示缓存 |
| Filter | 打开筛选器 |
| Search | 搜索文件、Tag、线程、地址 |
| Track Leaks | 是否记录分配来源 |
| Show Empty Pages | 是否显示空 Page |
| Show System Allocs | 是否显示系统大对象分配 |
| Thread Filter | 选择全部线程或单个 Arena |
| Size Range | 设置分配大小范围 |
| Refresh Rate | 选择刷新频率 |
| Export | 导出 JSON/CSV |

## 5.2 推荐刷新频率

```text
Paused
1 Hz
5 Hz
10 Hz
Every Frame
```

默认：

```text
5 Hz
```

Allocator Runtime 不应每帧构建大量临时字符串。UI 层按固定周期获取结构化 Snapshot。

---

# 6. Summary Cards

顶部显示以下核心指标：

| 指标 | 说明 |
|---|---|
| Live Requested | 当前用户请求的有效字节 |
| Live Reserved | 当前保留的 Page 和系统内存 |
| Peak Requested | Requested 峰值 |
| Peak Reserved | Reserved 峰值 |
| Active Allocations | 当前尚未释放的分配数量 |
| Alloc Rate | 最近一秒分配字节速率 |
| Free Rate | 最近一秒释放字节速率 |
| Page Count | 当前 Page 总数 |
| Cached Empty Pages | 缓存空 Page 数 |
| System Allocations | 当前系统大对象分配数量 |
| Leak Count | 当前追踪到的未释放分配 |
| Fragmentation | Reserved / Requested |

## 6.1 碎片率定义

建议：

```text
fragmentation_ratio = reserved_bytes / max(requested_bytes, 1)
```

同时可增加：

```text
internal_fragmentation_bytes =
    active_block_capacity_bytes - requested_small_allocation_bytes
```

注意区分：

- External/Reserved Fragmentation；
- Bin 内部碎片；
- Empty Page 缓存。

---

# 7. Memory Trend 图表

显示最近一段时间的：

- Requested Bytes；
- Reserved Bytes；
- Active Allocation Count；
- Allocation Rate；
- Free Rate。

第一版默认只显示：

```text
Requested Bytes
Reserved Bytes
```

## 7.1 采样数据

```cpp
struct MemoryTimelineSample
{
    f64 _time_sec = 0.0;
    u64 _requested_bytes = 0u;
    u64 _reserved_bytes = 0u;
    u64 _active_allocations = 0u;
    u64 _allocation_rate = 0u;
    u64 _free_rate = 0u;
};
```

建议使用固定容量环形缓冲：

```text
60 秒 × 10 Hz = 600 个 Sample
```

不得每次刷新都重新分配整个数组。

---

# 8. Bin Statistics

## 8.1 表格列

| 列 | 说明 |
|---|---|
| Bin Size | Size Class 的 Block Size |
| Partial Pages | 部分占用 Page 数 |
| Full Pages | 满 Page 数 |
| Empty Pages | 空 Page 数 |
| Active Blocks | 当前已用 Block 数 |
| Free Blocks | 当前可用 Block 数 |
| Peak Blocks | 历史峰值 |
| Requested Bytes | 用户请求字节总和 |
| Capacity Bytes | Active Block 容量 |
| Reserved Bytes | Page 占用总量 |
| Hit Rate | 该 Bin 命中率 |
| Internal Fragmentation | Block Capacity / Requested |
| Alloc Rate | 最近采样周期分配数 |
| Free Rate | 最近采样周期释放数 |
| Remote Free Count | 跨线程释放数量 |
| Pending Remote Free | 尚未归并数量 |

第一版如果尚未实现 Remote Free，可以显示为 0，并在代码中保留字段。

## 8.2 Bin Snapshot

```cpp
struct AllocatorBinSnapshot
{
    u32 _bin_index = 0u;
    u32 _block_size = 0u;

    u32 _partial_page_count = 0u;
    u32 _full_page_count = 0u;
    u32 _empty_page_count = 0u;

    u64 _active_block_count = 0u;
    u64 _free_block_count = 0u;
    u64 _peak_block_count = 0u;

    u64 _requested_bytes = 0u;
    u64 _capacity_bytes = 0u;
    u64 _reserved_bytes = 0u;

    u64 _alloc_count = 0u;
    u64 _free_count = 0u;
    u64 _remote_free_count = 0u;
    u64 _pending_remote_free_count = 0u;
};
```

## 8.3 行选择

单击 Bin 行：

- 更新 Page Detail。
- 显示该 Bin 的 Page 列表。
- 过滤 Recent Allocation Events。
- 图表可切换为该 Bin 的局部趋势。

双击 Bin 行：

- 打开详细 Bin Inspector。
- 显示该 Bin 所有 Page。

---

# 9. Page Detail

## 9.1 基础信息

显示：

```text
Page Address
Owner Arena
Owner Thread
Bin Index
Block Size
Block Count
Used Count
Free Count
Page State
Block Begin
Block End
Reserved Bytes
Committed Bytes
Remote Free Count
```

## 9.2 Page 占用图

每个 Block 用一个小方格表示：

| 状态 | 建议视觉 |
|---|---|
| Used | 绿色 |
| Free | 蓝色 |
| Pending Remote Free | 紫色 |
| Guard/Metadata | 黄色 |
| Invalid/Corrupted | 红色 |
| Uncommitted/System | 灰色 |

不要在实现中硬编码颜色，使用 Editor Theme 中的语义颜色。

## 9.3 Page Snapshot

```cpp
enum class EAllocatorPageState : u8
{
    kEmpty,
    kPartial,
    kFull,
};

enum class EAllocatorBlockState : u8
{
    kFree,
    kUsed,
    kPendingRemoteFree,
    kCorrupted,
};

struct AllocatorPageSnapshot
{
    u64 _page_address = 0u;
    u64 _block_begin = 0u;
    u64 _block_end = 0u;

    u32 _arena_id = 0u;
    u32 _thread_id = 0u;
    u32 _bin_index = 0u;

    u32 _block_size = 0u;
    u32 _block_count = 0u;
    u32 _used_count = 0u;
    u32 _free_count = 0u;
    u32 _pending_remote_free_count = 0u;

    EAllocatorPageState _state = EAllocatorPageState::kEmpty;
    Vector<EAllocatorBlockState> _block_states;
};
```

## 9.4 Block 状态采集

Release 或低开销 Profiling 模式下，可以只采集：

```text
Used Count
Free Count
```

只有 Debug Tracking 模式才构建 `_block_states`。

推荐使用 Debug 位图，而不是遍历 Free List 推导所有 Block：

```cpp
DebugBitSet _allocated_blocks;
DebugBitSet _pending_remote_blocks;
```

原因：

- 可检测 Double Free；
- 可快速生成 Page Occupancy；
- 不需要扫描链表；
- 可验证 `_free_count` 与位图一致。

---

# 10. Arena / Thread Statistics

## 10.1 表格列

| 列 | 说明 |
|---|---|
| Arena | Arena ID |
| Thread | 线程名称 |
| Thread ID | 系统线程 ID |
| Allocations | 总分配次数 |
| Frees | 总释放次数 |
| Local Frees | 本线程释放 |
| Remote Frees | 跨线程释放 |
| Pending Remote Frees | 尚未处理 |
| Pages Owned | 拥有 Page 数 |
| Requested Bytes | 当前请求字节 |
| Reserved Bytes | 当前保留字节 |
| Peak Requested | 峰值 |
| Lock Wait | Arena 锁等待时间，可选 |

## 10.2 Arena Snapshot

```cpp
struct AllocatorArenaSnapshot
{
    u32 _arena_id = 0u;
    u64 _thread_id = 0u;
    String _thread_name;

    u64 _allocation_count = 0u;
    u64 _free_count = 0u;
    u64 _local_free_count = 0u;
    u64 _remote_free_count = 0u;
    u64 _pending_remote_free_count = 0u;

    u64 _page_count = 0u;
    u64 _requested_bytes = 0u;
    u64 _reserved_bytes = 0u;
    u64 _peak_requested_bytes = 0u;
};
```

避免在 PageHeader 中保存临时字符串的 `c_str()`。线程名称应保存在 Arena 或调试服务的稳定 `String` 中。

---

# 11. Recent Allocation Events

## 11.1 事件类型

```cpp
enum class EMemoryEventType : u8
{
    kAllocate,
    kFree,
    kRemoteFree,
    kPageCreate,
    kPageRelease,
    kInvalidFree,
    kDoubleFree,
    kGuardCorruption,
    kLeak,
};
```

## 11.2 事件结构

```cpp
struct MemoryDebugEvent
{
    u64 _sequence = 0u;
    f64 _time_sec = 0.0;

    EMemoryEventType _type = EMemoryEventType::kAllocate;

    u64 _thread_id = 0u;
    u32 _arena_id = 0u;
    u32 _bin_index = 0u;

    u64 _address = 0u;
    u64 _raw_address = 0u;
    u64 _requested_size = 0u;
    u64 _actual_size = 0u;
    u32 _alignment = 0u;

    const char *_file = nullptr;
    const char *_function = nullptr;
    const char *_tag = nullptr;
    u32 _line = 0u;
};
```

## 11.3 UI 列

```text
Time
Sequence
Thread
Arena
Operation
Requested Size
Actual Size
Alignment
Address
Bin
File
Line
Function
Tag
```

## 11.4 事件缓存

必须使用固定容量 Ring Buffer：

```text
默认容量：50,000 条
```

不得无限增长。

事件写入要求：

- 不使用通用 Allocator 再次分配。
- 不构造动态字符串。
- `file/function/tag` 使用静态字符串指针或 Interned String ID。
- 超出容量覆盖最旧事件。
- 记录 dropped event count。

建议：

```cpp
class MemoryEventRingBuffer
{
public:
    bool Push(const MemoryDebugEvent &event);
    u32 CopyRecent(Span<MemoryDebugEvent> output) const;
};
```

第一版可以使用 Mutex 保护；后续再优化为 MPSC Ring Buffer。

---

# 12. Leak 与错误区域

## 12.1 统计卡片

显示：

```text
Leaks
Double Free
Invalid Free
Corruption Before Guard
Corruption After Guard
Header Corruption
Page State Mismatch
Dropped Events
```

## 12.2 错误记录

错误记录应包含：

```text
类型
时间
地址
大小
线程
Arena
文件
行号
函数
Tag
说明
```

点击错误：

- 自动选中对应 Bin；
- 自动选中对应 Page；
- 在 Recent Events 中过滤该地址；
- 可复制地址和来源；
- 可跳转源码。

## 12.3 源码跳转

提供 Editor 接口：

```cpp
void OpenSourceFile(const String &file, u32 line);
```

双击文件行时调用。

---

# 13. Snapshot 与 Diff

## 13.1 Snapshot 内容

```cpp
struct AllocatorSnapshot
{
    u64 _snapshot_id = 0u;
    f64 _time_sec = 0.0;

    AllocatorGlobalSnapshot _global;
    Vector<AllocatorBinSnapshot> _bins;
    Vector<AllocatorArenaSnapshot> _arenas;
    Vector<AllocatorAllocationSnapshot> _allocations;
};
```

## 13.2 Allocation Snapshot

只有 Track Leaks 开启时采集：

```cpp
struct AllocatorAllocationSnapshot
{
    u64 _address = 0u;
    u64 _requested_size = 0u;
    u64 _actual_size = 0u;
    u32 _alignment = 0u;

    u64 _thread_id = 0u;
    u32 _arena_id = 0u;
    u32 _bin_index = 0u;

    const char *_file = nullptr;
    const char *_function = nullptr;
    const char *_tag = nullptr;
    u32 _line = 0u;
};
```

## 13.3 Diff 规则

Snapshot B - Snapshot A：

- Added Allocations；
- Freed Allocations；
- Persistent Allocations；
- Requested Bytes Delta；
- Reserved Bytes Delta；
- Page Count Delta；
- 各 Bin Delta；
- 各 Tag Delta。

优先按地址比较：

```text
A 中不存在，B 中存在 → Added
A 中存在，B 中不存在 → Freed
A、B 都存在          → Persistent
```

---

# 14. Tag 系统

为了让面板具有实际价值，需要为分配增加 Tag。

推荐 API：

```cpp
void *Allocate(
    u64 size,
    u64 alignment,
    const char *file,
    const char *function,
    u32 line,
    const char *tag);
```

宏：

```cpp
#define AL_ALLOC_TAG(type, count, tag) \
    static_cast<type *>(Ailu::Core::Allocator::Get().Allocate( \
        sizeof(type) * static_cast<u64>(count), \
        alignof(type), \
        __FILE__, \
        __FUNCTION__, \
        __LINE__, \
        tag))
```

建议首批 Tag：

```text
Default
RenderGraph
Renderer
SpriteBatch
UI
Physics
ECS
Scene
Resource
AssetImport
Script
Audio
Editor
JobSystem
Temporary
```

Tag 必须是稳定字符串或 Intern ID，不应在 Allocate 时动态创建 `String`。

---

# 15. Allocator Runtime 接口改造

建议增加只读统计接口。

## 15.1 全局 Snapshot

```cpp
struct AllocatorGlobalSnapshot
{
    u64 _requested_bytes = 0u;
    u64 _reserved_bytes = 0u;
    u64 _peak_requested_bytes = 0u;
    u64 _peak_reserved_bytes = 0u;

    u64 _active_allocation_count = 0u;
    u64 _total_allocation_count = 0u;
    u64 _total_free_count = 0u;

    u64 _page_count = 0u;
    u64 _cached_empty_page_count = 0u;
    u64 _system_allocation_count = 0u;

    u64 _double_free_count = 0u;
    u64 _invalid_free_count = 0u;
    u64 _guard_corruption_count = 0u;
    u64 _dropped_event_count = 0u;
};
```

## 15.2 推荐接口

```cpp
class Allocator
{
public:
    void CaptureGlobalSnapshot(AllocatorGlobalSnapshot &snapshot) const;
    void CaptureBinSnapshots(Vector<AllocatorBinSnapshot> &snapshots) const;
    void CaptureArenaSnapshots(Vector<AllocatorArenaSnapshot> &snapshots) const;
    bool CapturePageSnapshot(u64 page_address, AllocatorPageSnapshot &snapshot) const;
    void CaptureActiveAllocations(Vector<AllocatorAllocationSnapshot> &snapshots) const;
};
```

## 15.3 锁策略

第一版允许：

```text
Capture Snapshot 时获取 Allocator 全局锁
```

但必须：

- 在锁内只复制 POD 数据；
- 不在锁内执行 ImGui；
- 不在锁内格式化字符串；
- 不在锁内排序；
- 不在锁内导出文件；
- 不在锁内触发日志。

后续可使用双缓冲统计或原子计数减少停顿。

---

# 16. MemoryDebugService

建议实现一个独立服务：

```cpp
class MemoryDebugService
{
public:
    static MemoryDebugService &Get();

    void Tick(f32 delta_time);
    void SetPaused(bool paused);
    void SetRefreshInterval(f32 interval_sec);

    const AllocatorSnapshot &CurrentSnapshot() const;
    const Vector<MemoryTimelineSample> &Timeline() const;
    const Vector<MemoryDebugEvent> &RecentEvents() const;

    u64 CreateSnapshot();
    bool DiffSnapshots(u64 lhs_id, u64 rhs_id, AllocatorSnapshotDiff &diff);

    void ClearEvents();
    void ExportJson(const Path &path) const;
    void ExportCsv(const Path &path) const;
};
```

职责：

- 定期调用 Allocator Snapshot API；
- 缓存 UI 数据；
- 管理时间序列；
- 管理 Snapshot；
- 管理过滤器；
- 复制事件 Ring Buffer；
- 处理导出；
- 不负责绘制。

---

# 17. Editor Panel 类设计

```cpp
class AllocatorDebugPanel final : public EditorWindow
{
public:
    void OnOpen() override;
    void OnClose() override;
    void OnGUI() override;

private:
    void DrawToolbar();
    void DrawSummary();
    void DrawMemoryTimeline();
    void DrawBinTable();
    void DrawPageDetail();
    void DrawArenaTable();
    void DrawEventTable();
    void DrawErrorPanel();
    void DrawSnapshotDiff();

    void UpdateSelectedPage();
    void ApplyFilters();

private:
    bool _paused = false;
    bool _track_leaks = true;
    bool _show_empty_pages = true;
    bool _show_system_allocations = true;

    u32 _selected_bin_index = kInvalidIndex;
    u64 _selected_page_address = 0u;
    u32 _selected_arena_id = kInvalidIndex;

    i32 _thread_filter = -1;
    u64 _min_size = 0u;
    u64 _max_size = 64u * 1024u;

    String _search_text;
};
```

---

# 18. ImGui 实现建议

## 18.1 表格

使用：

```cpp
ImGui::BeginTable(...)
```

建议开启：

```text
ImGuiTableFlags_Resizable
ImGuiTableFlags_Reorderable
ImGuiTableFlags_Hideable
ImGuiTableFlags_Sortable
ImGuiTableFlags_RowBg
ImGuiTableFlags_BordersInnerV
ImGuiTableFlags_ScrollY
```

Bin 和 Event 表格需要支持排序。

## 18.2 图表

如果项目已集成 ImPlot，使用：

```cpp
ImPlot::BeginPlot()
ImPlot::PlotLine()
ImPlot::PlotShaded()
```

避免自己手工绘制坐标轴。

## 18.3 Page Block Grid

使用 `ImDrawList` 绘制：

```cpp
draw_list->AddRectFilled(...)
```

每个方格：

```text
8×8 或 10×10 像素
间距 1～2 像素
```

Hover 时显示 Tooltip：

```text
Block Index
State
Address
Requested Size
Allocation Source
```

## 18.4 性能要求

UI 不应：

- 每帧复制全部 active allocation；
- 每帧对全部事件排序；
- 每帧创建大量 `String`；
- 每个 Block 调用复杂格式化；
- 在 Draw 中持有 Allocator 锁。

推荐：

- Snapshot 频率默认 5 Hz；
- Event 表仅显示过滤后的最近 1000 条；
- Active Allocation 列表仅在打开 Leak 页面时获取；
- Page Occupancy 仅获取选中 Page。

---

# 19. 调试级别

建议定义：

```cpp
enum class EMemoryTrackingLevel : u8
{
    kDisabled,
    kCounters,
    kEvents,
    kFull,
};
```

## 19.1 Disabled

- 无额外统计。
- Release 默认。

## 19.2 Counters

- 全局计数；
- Bin/Page/Arena 计数；
- 不记录文件和事件。

## 19.3 Events

- Counters；
- 环形事件缓存；
- Tag、线程和地址。

## 19.4 Full

- Events；
- Active Allocation Map；
- Guard；
- Double Free 位图；
- Page Block 状态；
- Snapshot Diff。

---

# 20. 编译开关

建议：

```cmake
option(AILU_ENABLE_MEMORY_DEBUG_PANEL "Enable allocator debug panel" ON)
option(AILU_ENABLE_MEMORY_TRACKING "Enable allocator tracking" ON)
option(AILU_ENABLE_MEMORY_GUARDS "Enable memory guard validation" OFF)
```

编译定义：

```cmake
target_compile_definitions(Engine PUBLIC
    AILU_ENABLE_MEMORY_TRACKING=$<BOOL:${AILU_ENABLE_MEMORY_TRACKING}>
)

target_compile_definitions(Editor PRIVATE
    AILU_ENABLE_MEMORY_DEBUG_PANEL=$<BOOL:${AILU_ENABLE_MEMORY_DEBUG_PANEL}>
)
```

---

# 21. 实现阶段

## Phase 1：只读统计

- [ ] 定义 Global/Bin/Page/Arena Snapshot。
- [ ] Allocator 提供 Capture API。
- [ ] 实现 Summary Cards。
- [ ] 实现 Bin Table。
- [ ] 实现 Page Detail。
- [ ] 实现 Arena Table。
- [ ] 不实现事件和 Leak Diff。

验收：

- 面板打开后不会改变分配结果。
- Page 的 Used + Free 等于 Block Count。
- Bin Page 数与 PageMgr 一致。
- UI 无明显卡顿。

## Phase 2：趋势图和事件

- [ ] MemoryDebugService。
- [ ] 固定容量 Timeline。
- [ ] Event Ring Buffer。
- [ ] Recent Events Table。
- [ ] 搜索与过滤。
- [ ] Export JSON/CSV。

验收：

- 连续运行 30 分钟，事件缓存内存不增长。
- Pause 后 UI 数据冻结，但运行时可继续。
- Clear Events 不影响 Allocator 状态。

## Phase 3：泄漏与错误

- [ ] Active Allocation Map。
- [ ] Double Free 检测。
- [ ] Invalid Free 检测。
- [ ] Guard 检测。
- [ ] Error Panel。
- [ ] 源码跳转。

验收：

- 故意泄漏时可定位文件和行。
- Double Free 显示一次明确错误。
- 越界可显示 Before/After Guard。
- 错误报告不依赖 LogMgr。

## Phase 4：Snapshot Diff

- [ ] 创建 Snapshot。
- [ ] Snapshot 列表。
- [ ] Diff。
- [ ] 按 Tag、文件、线程聚合。
- [ ] Persistent Allocation 分析。

验收：

- A/B 快照能正确识别 Added、Freed 和 Persistent。
- Diff 结果可导出。
- 快照操作不会长时间阻塞主线程。

---

# 22. 验收标准

## 22.1 正确性

- [ ] Global Requested 与 Active Allocation 总和一致。
- [ ] Reserved 与 Page/System Allocation 总和一致。
- [ ] 每个 Page：Used + Free = Block Count。
- [ ] 每个 Bin Page 数与实际链表一致。
- [ ] Empty Page 的 Free Count 等于 Block Count。
- [ ] Full Page 的 Free Count 为 0。
- [ ] Page 地址可正确映射到 Bin 和 Arena。
- [ ] 系统大对象不会被误认为 Page 分配。
- [ ] 面板关闭后不继续采集不必要的 Full Tracking 数据。

## 22.2 稳定性

- [ ] ASan 下运行无错误。
- [ ] 面板打开 1 小时无持续内存增长。
- [ ] 频繁 Pause/Resume 无崩溃。
- [ ] 频繁 Snapshot/Diff 无悬空引用。
- [ ] Allocator Shutdown 时面板已安全停止采样。
- [ ] Editor 关闭顺序不会调用已销毁的 LogMgr。

## 22.3 性能

- [ ] Counters 模式额外成本低于 2%。
- [ ] Events 模式额外成本低于 5%。
- [ ] UI 默认刷新不超过 5 Hz。
- [ ] 单次 Snapshot 锁占用时间可测量。
- [ ] Event Ring Buffer 不发生动态扩容。
- [ ] UI 绘制不直接读取 Allocator 内部链表。

---

# 23. 建议的第一版最小功能

第一版只做以下内容：

```text
Summary
Memory Trend
Bin Table
Page Detail
Arena Table
Recent Events
Basic Errors
```

暂不做：

```text
Call Stack
Flame Graph
完整 Heap Snapshot
远程进程
无锁事件队列
```

这已经足够验证当前 Page-local Free List、Bin 状态迁移和后续 Arena 改造。

---

# 24. 参考界面中的信息层级

视觉优先级：

1. 红色错误与 Leak。
2. Requested/Reserved 与峰值。
3. Bin 碎片和 Page 状态。
4. Arena 分配压力。
5. Recent Events。
6. 次要地址和原始字段。

默认状态下避免满屏红色。只有异常指标使用错误颜色。

---

# 25. Claude 实现任务描述

可以直接将以下内容作为实现任务输入：

```text
请在 AiluEngine Editor 中实现 AllocatorDebugPanel。

要求：
1. 运行时 Allocator 不依赖 Editor、ImGui 或 LogMgr。
2. 新增 AllocatorGlobalSnapshot、AllocatorBinSnapshot、
   AllocatorPageSnapshot 和 AllocatorArenaSnapshot。
3. Allocator 提供只读 Capture API，Snapshot 时只在锁内复制 POD 数据。
4. 新增 MemoryDebugService，默认 5 Hz 采样，并维护 60 秒环形时间序列。
5. 新增固定容量 MemoryDebugEvent Ring Buffer，不允许无限增长。
6. 使用 ImGui + ImPlot 实现 Summary、Memory Trend、Bin Table、
   Page Detail、Arena Table、Recent Events 和 Error Panel。
7. Bin 行可选择；选择后显示该 Bin 的 Page，并显示 Block Occupancy。
8. 支持 Pause、Snapshot、Clear、Search、Thread Filter、Size Filter。
9. 所有字符串和事件记录不得在 Allocator 热路径中动态分配。
10. 保持代码风格：
    - 变量名使用小写加下划线；
    - 类成员以下划线开头；
    - 静态变量以 s 开头；
    - 常量以小写 k 开头并使用驼峰；
    - 类名和函数名使用驼峰；
    - 每行不超过 120 列。
11. 第一版允许使用 Mutex，不需要实现无锁队列。
12. 提供最小测试和一个 Debug 菜单入口。
```

---

# 26. 最终交付物

实现完成后应包含：

```text
Engine/Inc/Framework/Common/MemoryDebugTypes.h
Engine/Inc/Framework/Common/MemoryDebugService.h
Engine/Src/Framework/Common/MemoryDebugService.cpp

Editor/Inc/Windows/AllocatorDebugPanel.h
Editor/Src/Windows/AllocatorDebugPanel.cpp
```

可能需要修改：

```text
Allocator.hpp
Allocator.cpp
Editor Window Registry
Editor Menu
CMakeLists.txt
```

同时提交：

- 至少一个 Snapshot 单元测试；
- Event Ring Buffer 测试；
- Page Snapshot 一致性测试；
- 面板截图；
- 5 分钟压力运行结果。
