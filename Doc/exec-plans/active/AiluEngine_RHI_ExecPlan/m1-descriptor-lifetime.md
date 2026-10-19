# M1 — Descriptor Lifetime Safety (Dynamic Ring + Bindless)

**Status:** TODO　**Prerequisite:** M0 PASSED　**Exit gate:** 全面消除 Descriptor 使用期间提前覆盖或复用的可能性，并有小容量压力测试证明。

## Purpose

在保留现有单 MainHeap、持久 Bindless 区和动态 Descriptor 拷贝模型的前提下，为动态 Ring 和 Bindless Index 建立统一但不过度设计的**占用、提交、完成、回收**协议。

## Scope

`GPUVisibleDescriptorAllocator::GetOrCopy / GetBindGpuHandle / GetBindHandle`、`CommitDescriptorsForDraw/Dispatch`、`AllocBindless*Index/ReleaseBindless*Index` 及其调用方、D3DCommandBuffer 提交回调及 Fence 完成收集。

## Non-goals

- 不重新设计 RootSignature / Bindless Shader Layout；不强制改成一个 Descriptor Heap/CommandList。
- 不利用单纯扩大 Heap 来掩盖回绕问题。
- 不靠 `GetFrameCount()` 或硬编码 N 帧延迟释放。

## Architecture Constraints

- GPU 尚未完成引用时，对应 Shader-visible 槽不可写入或重用；CPU 已释放不能直接视为 GPU 已完成。
- 「未提交的已录制 CommandList」也占有 Descriptor；禁止因为当前 GPU Fence 已完成就回收这些 Pending 槽。
- 并行录制时 Descriptor 分配顺序不等于 Queue 提交顺序，不能仅用简单提交 Fence 的数值大小推断 Ring 每个位置可回收。
- Descriptor Cache 共享的 GPU 槽若被多个 CommandList 引用，必须覆盖**所有使用者的完成点**；不能只追踪首次分配的 CommandList。
- Shader-visible Heap 切换会影响绑定表/Root Table；绝不做隐式 Heap 切换的粗暴 fallback。

## Baseline / Evidence

- `DescriptorManager.cpp::GetOrCopy` 在 `_ring_cursor + DescriptorNum > kMainHeapDescriptorNum` 时直接设回 `_ring_base_index` 并自增 `_heap_epoch`。
- `_descriptor_caches` 用 frame/epoch/generation 判断命中；这不是 GPU 生命周期依据。
- `ReleaseBindlessSRVIndex/ReleaseBindlessUAVIndex` 设置 `_inuse=0` 后立即 `_free.push_back(index)`；是否已有调用端 Fence 保护需要第一步确认。
- `D3DTexture.cpp` 等调用 `GetBindGpuHandle` 的路径可能**没有直接的 CommandList 参数**，不能假设修改 `CommitDescriptorsForDraw` 就覆盖所有 GPU 使用者。

## Target Outcome

- 一个可解释的生命周期状态机：`Available → RecordingOwned → Submitted(fence) → Completed → Available`；取消录制时 `RecordingOwned → Available`（确认无 GPU 引用后）。
- Bindless 对称支持 `Active → RetirePending(last_use_fence) → Free`；更新 Descriptor 内容也需要处理在途读取。
- 小 Ring 高并发压力下不会覆盖仍使用的槽；空间不足时有明确、安全的容量耗尽行为。

## Relevant Files

`Inc/RHI/DX12/DescriptorManager.h`, `Src/RHI/DX12/DescriptorManager.cpp`, `Inc/RHI/DX12/D3DCommandBuffer.h`, `Src/RHI/DX12/D3DCommandBuffer.cpp`, `Src/RHI/DX12/D3DContext.cpp`, `D3DTexture.cpp`, `D3DBuffer.cpp`, `D3DShader.cpp`；必要时 `Inc/Render/Texture.h` / 高层 Bindless 资源句柄。

## Implementation Steps

### Step 1 — 真实调用链与最小复现

- 搜索全部 `GetBindGpuHandle`, `GetBindHandle`, `CommitDescriptorsFor*`, `AllocBindless*Index`, `ReleaseBindless*Index` 及所有直接通过 MainHeap CPU Handle 写 Descriptor 的路径。
- 每处注明「是否只有 CPU 临时查询」「何时 GPU 实际引用」「哪条 CommandList」「何时回收」。
- 检查各 `D3DTexture` View/Buffer View 的删除时机是否已经由 `GpuResourceRegistry` 或 Queue Fence 拦截；有证明可复用，**不重复延迟**。
- 用 M0 的小容量用例判断实际危险条件；保留失败记录以作修复前证据。

### Step 2 — Dynamic Ring 生命周期建模

推荐实现：**记录级 Reservation（区间 + Owner + 状态），提交回填 Fence、完成后回收**。可用线性序号 + 环形槽位 + Pending 队列实现，避免引入复杂通用分配器。

- `DescriptorReservation`（示意）包含 `begin_index/count`, `recording_owner`, `submitted_fence`, `status`；一个 CommandList 可有多个连续或不连续区间。
- 分配时所有仍为 RecordingOwned/Submitted 且未完成的区间都视为占用；只有确认某区间不再被**任意**在途 CommandList 引用才回收。
- 录制取消、Close、提交失败、正常提交、GPU Fence 完成等路径必须都有对称处理，分别记录统计。
- 对同一 Descriptor 缓存：优先**限制为 CommandList 局部缓存**以保持寿命明晰；若保留跨命令缓存，需要显式使用者引用或引用 Fence 的 union，不得借帧号偷懒。
- 并行录制顺序和提交顺序不同：允许前方尚未退休区间阻塞回绕；不得按 `fence_value <= completed` 直接跳过其他 Pending 区间。
- 单 Heap 当前无法安全容纳全部待录制命令时，先提供可诊断的 `AllocationFailure/CapacityExceeded`；再评估**显式提交/回收点或限流录制**。严禁静默重用槽或原地更换 Heap 却不重绑 Root Tables。

### Step 3 — Persistent Bindless Index 退役

- 按实际使用者追踪最后使用 Fence；仅当所有可能引用索引的提交完成后，索引可重新加入 freelist。
- Descriptor 原地更新（同槽改指向另一资源）也需依照**旧内容是否仍在途使用**判断，而不只是 index 是否仍 Active。
- `count>1` 的 Bindless 连续分配和释放要在测试中覆盖；当前释放接口只接受单个 index，需核实真实契约。若代码只允许单个释放，在文档和断言中明确，不隐式支持多槽。
- 退役索引需要避免资源生命周期循环引用；依赖 GPU fence/event 与轻量记录即可。

### Step 4 — 错误处理、统计、无回归

- 为 Ring 添加 `used/peak/pending/retired/overflow`，Bindless 添加 `active/pending_retire/free` 计数；所有路径使用相同线程同步规则。
- 确认 MainHeap 区间始终不交叉：reserved index 0、SRV、UAV、Ring；检测 `alloc.DescriptorNum()` 大于可用 Ring 总容量和整数溢出。
- 复核 `_pages` staging allocation 的 Generation、Buffer/Tex View 与 Shader cache 的引用，保证 cache 不可能返回已被复用的 GPU handle。

## Test Matrix

| 场景 | 操作 | 预期 |
|---|---|---|
| Ring wrap | 极小 Ring，GPU Fence 故意延迟 | 不写入仍占用的槽，溢出受控 |
| Parallel | CommandList A 先录、B 后录；B 先提交 | A Pending 不可提前回收 |
| Multi-use cache | 相同 SRV 在多个命令使用 | 最后使用者完成前不可复用 |
| Bindless free/realloc | SRV/UAV 索引释放后立即请求新索引 | 旧 GPU 引用未完成时索引不被覆盖 |
| Bindless rewrite | 旧 Descriptor 被提交，CPU 随后改写 | 必须延迟或采用新槽 |
| Resize / shutdown | swapchain RT 释放，Command Pool 回收 | 无 DXGI invalid call / use-after-free |
| Long run | >10k 次 alloc/free/submission | 稳定、有界、没有指数增长 |

## Implementation Blueprint — Descriptor 生命周期的可落地方案

> 本阶段只处理 Descriptor 的**内容不被在途 GPU 覆盖**问题。不要同时改 RootSignature、Shader Binding Layout、Heap 数量或 RenderGraph。先用 M0 的 BASE-04/05 固定故障条件，再分 Dynamic Ring 和 Bindless 两个小提交完成。

### M1-A. 必读代码及调用链清单

按下面顺序打开源码，逐个读完再改：

1. `DescriptorManager.h/.cpp`：`GPUVisibleDescriptorAllocator::{Allocate,Free,GetOrCopy,GetBindGpuHandle,GetBindHandle,CommitDescriptorsForDraw,CommitDescriptorsForDispatch,AllocBindless*Index,ReleaseBindless*Index}`；确认 `kMainHeapDescriptorNum=131072`，`kBindlessSRVCapacity=65536`、`kBindlessUAVCapacity=16384` 与 `_ring_base_index`。
2. `D3DTexture.cpp`：`GetBindGpuHandle` 返回 `.ptr` 的多个 View 获取函数（目前存在直接返回裸 GPU Handle 的路径）。追踪取得 Handle 后实际写入哪条 CommandList；不能只改 `CommitDescriptorsForDraw/Dispatch`。
3. `D3DBuffer.cpp`：`CreateBindlessBufferSrv/Uav`、`ReleaseBindlessSrvIndex/UavIndex`、Buffer resize/update 时释放旧索引再创建新索引的路径。
4. `D3DCommandBuffer.cpp`：`Clear()`、`MarkSubmitted()`、`RunPostSubmitCallbacks()`、`IsReady()`；记录未提交、已提交、已完成的真实切换位置。
5. `D3DContext.cpp::ExecuteRHICommandBuffers`：Fence `submitted_fence` 在 `m_commandQueue->Signal()` 成功后得到；原生列表一起提交；`GpuCommandWorker` 可并行录制。

制作 `descriptor-callsite-audit.md` 表格：`file:line | SRV/UAV/CBV | 生成时刻 | 最终 GPU 使用者 | 录制 owner | 提交 fence | 释放时刻 | 是否可直接重写`。**表格中任何一行 owner 不明就不得修改正式 allocator。**

### M1-B. 动态 Descriptor Ring 状态机与数据约束

当前区间可抽象为 `[ring_begin, ring_end)`，其中 `ring_begin = 1 + 65536 + 16384`、`ring_end = 131072`（实际运行通过成员计算而非到处写魔数）。单次 `DescriptorNum()` 必须大于 0 且不超过 ring 总容量。

同一槽的合法流程：

```text
Free
  ├─ Acquire(owner) ─> RecordingOwned
  │                    ├─ Submit(owner, fence) ─> Submitted(fence)
  │                    │                          └─ CompletedFence >= fence ─> Free
  │                    └─ Cancel(owner) ─> Free
  └─ 无法获得 → CapacityExceeded（不覆写）
```

`RecordingOwned` 不能因当前 GPU 完成值很大而回收；`Submitted` 不能因已跨 N 帧而回收。**同一 Descriptor 内容被多个 CommandList 复用时，必须等全部用户完成。** 建议第一版禁止跨 CommandList 缓存，采用局部缓存；保留 staging Descriptor generation 与 Ring epoch 用来失效旧缓存，但它们不承担寿命证明。

推荐最小结构（伪代码，类型、成员名与工程实际契约对齐）：

```cpp
struct DescriptorReservation
{
    u32 _begin_index = 0;
    u32 _descriptor_count = 0;
    u64 _recording_owner_id = 0;
    u64 _submitted_fence = 0;
    bool _is_submitted = false;
};

struct DescriptorUseToken
{
    u64 _reservation_id = 0; // 本次 GPU 引用的唯一标识；复用记录需共享或复制使用关系
};
```

此结构只表达语义，**不得**未经验证就把全局记录写成 `std::queue` 并按 Fence 值从头弹出：分配、提交可能乱序，且缓存可能多用户。推荐两种最小方案任选其一：

- **方案 A（优先）：** CommandList-local Descriptor cache；每次首次 `GetBind*` 携带 `D3DCommandBuffer/recording_owner`，Owner 持有 reservation 列表，提交时批量标记 Fence；某 Owner 取消时才释放其未提交槽。全局分配器用循环区间搜索或空闲区间管理，不以提交顺序猜测所有权。
- **方案 B（仅调用点迁移非常困难时）：** 共享 Reservation 带使用者集合/引用计数，最后一个使用者完成才退役。必须单测覆盖 `A/B` 共享、先后提交逆序、一个取消另一个已提交。

**不可采用**「只给 allocator 记 `_last_fence`」或「本帧缓存命中直接返回」的方案，因为 GPU Fence 只对应已提交工作，并不能覆盖还在录制的未来使用者。

### M1-C. 动态 Ring 的最小修改顺序

1. **定义 owner：** 在 `D3DCommandBuffer` 层增加稳定 recording ID / lifetime token，在 `Clear()` 前确认上一轮已完成；将该 ID 传入所有 GPU Descriptor Handle 获取路径。对于不能直接传参数的 Texture View getter，优先追到**调用它的 CommandBuffer 绑定点**再索取 Handle；避免使用线程局部全局 current-command，除非已证明异步与嵌套录制不会串 owner。
2. **修改 Cache scope：** 只缓存当前 CommandList 中的 staging descriptor id + generation → GPU reservation；跨命令列表不命中。`Clear()` 时删除该局部 cache；`GetBindGpuHandle` 如果未带 owner，必须返回受控错误/显式进入诊断支路，不能偷偷分配一个无主槽。
3. **添加 Reserve(count, owner)：** 持锁查找足够连续空间（允许尾部不足时从头找；任何 `[begin,end)` 与 Pending/Submitted 区间相交都不可写）；在 Reserve 成功后再 `CopyDescriptorsSimple`，不得先拷再验证。检查 `count==0`、超过容量、整数回绕、并发重复 reservation。
4. **Bind to command：** 每次实际把 GPU Handle 写入 CommandList 的时候，保证 owner 保有 reservation；如果 staging descriptor 可能被 CPU 修改，局部缓存需通过 staging generation 失效。防止同一 GPU 槽的内容被 CPU 改写而 cache 仍命中。
5. **Submit：** `ExecuteRHICommandBuffers` 成功 Queue Execute + Fence Signal 后，用返回的**真实 submitted_fence**更新本批 Owners 的 reservation。`MarkSubmitted/RunPostSubmitCallbacks` 可以作为挂钩，但不能在 Signal 前假定将来 Fence 一定成功。批量提交的 Owners 共享同一 Fence 是合法的。
6. **Cancel：** 不调用 Execute 的录制如果真正取消，才解除 `RecordingOwned`；请区分尚未提交被其他队列持有的 CommandList、重复 Close、失败后 Retry、Worker Stop。取消释放前检查 GPU 未看到其命令。
7. **Collect：** 用 `_p_cmd_buffer_fence->GetCompletedValue()` 收集已完成 reservation，整条路径持有正确同步锁；不在 draw 热路径遍历全部历史 reservation。迟滞释放避免 scan 全 Heap，必要时用 pending 列表记录有资格退休的实例。
8. **容量耗尽：** 优先返回可检测的失败结果并让调用者停止绑定/跳过本次 draw（Debug 可 assert；Release 必须能诊断）。仅在明确的帧边界允许等待特定已提交 Fence；绝对不能在持有 `_command_submit_mtx` 的录制线程里等待未来可能由该线程提交的 Fence。

### M1-D. Bindless index 与 Descriptor 内容分别保护

**请先区分两件事：** index 的 CPU 所有权释放，以及 GPU-visible 槽内 SRV/UAV Descriptor 的写入/覆盖。延迟索引释放不能阻止一个仍在使用的 index 被原地 `CreateShaderResourceView()` 改写。

建议状态如下：

```text
Free -> Active -> RetirePending -> Free
            \-> RenameOnUpdate (旧 index RetirePending，新 index Active)
```

实现细则：

1. `ReleaseBindless*Index` 不立即 `_free.push_back()`；先登记 `{index, generation, last_possible_use_fence, pending_recording_users}`，直到最后的**实际**提交完成才能释放。`_inuse=0` 必须表示真正进入 Free，而不是 RetirePending；增加状态枚举比依赖 0/1 更不易出错。
2. `D3DBuffer.cpp` 部分 resize/update 路径当前直接释放旧 index 并创建新 Descriptor；修改后旧 index pending retire，新 index 指向新资源，GPU Shader 常量里的 index 同步随资源版本更新。若旧 index 仍可能被未提交命令引用，必须保留其内容不变。
3. `D3DTexture.cpp` 各种 View create/destroy 同样采用旧 index 延迟回收。**不能**只保护 D3DResource COM 引用而让 Descriptor 槽先被新资源写入。
4. 若 Shader 通过 Bindless index 间接取 Descriptor，追踪 index 隶属的资源版本及 command owner；如无法枚举 GPU shader 对 index 的实际引用，保守地把该资源关联的所有可能 CommandList 作为使用者，直到安全边界，记录由此带来的额外滞留。
5. 单次 `AllocBindless*Index(count>1)` 需要区分连续段分配和单个释放。当前释放 API 仅接收一个 index；实施时**禁止误把 count 段当作一个槽释放**。若实际使用全为 count=1，写断言限制这一点并加入负面测试；如业务需要批量释放，再显式补 `(begin,count)` 及跟踪结构。
6. 设备销毁/Swapchain 全量 drain 前，可以通过统一 `WaitForGpu()` 作**停机或窗口销毁安全收尾**；不能把全 GPU wait 塞到每次 Release。

### M1-E. 关键测试必须包含具体执行/预期

| ID | 输入序列 | 正确结果 | 必须打印的证据 |
|---|---|---|---|
| DESC-01 | Ring 容量 8；A 占 6；未提交；B 请求 4 | 不覆盖 A；受控失败或获不冲突区间 | 区间列表、owner、状态 |
| DESC-02 | A 先录制占槽；B 后录制占槽；先提交 B | B 完成后 A 仍占槽 | 每次 completed fence、占用 bitmap |
| DESC-03 | A/B 使用相同 SRV，A 提交完成，B 未完成 | GPU descriptor 在 B 完成前不复用 | reservation users / last fence |
| DESC-04 | 关闭 A 而不提交；取消 A；分配 C | A 的专有 reservation 可再利用 | cancel 状态、调用路径 |
| DESC-05 | A 已提交，随后 Buffer resize 写新 SRV | 旧 index 内容仍对应旧 Buffer，直到 A 完成 | 旧/新 index、Readback CRC |
| DESC-06 | Texture view 创建/销毁 1 万次 | Active + RetirePending + Free 守恒 | 三类计数和高水位 |
| DESC-07 | 触发真实 Heap 容量耗尽 | 无 GPU in-flight 覆盖、无无限等待 | 错误码/警告、线程栈 |
| DESC-08 | 录制者在 Worker Stop 时取消 | 无 recording owner 泄漏、无双退役 | owner 释放计数 |

**GPU 对照测试：** 每个 Descriptor 指向含唯一模式数据的 Buffer/Texture（例如与 `resource_id` 对应的整数序列）；Shader 逐个读取后写入结果 Buffer，CPU Readback 验证精确内容。不能只检查 Debug Layer 无错误，因为有些槽覆盖只会造成画面内容错误。

### M1-F. 常见错误与修复手册

- **错误：** 从全局 Ring `alloc` 获得裸 Handle 但无法找到最后 GPU 使用者。**处理：** 先把调用移动到录制位置、显式传 Owner；不能默认按当前 frame 退休。
- **错误：** 同一 Handle 被不同 CommandList 缓存共享。**处理：** 优先改成 per-command 缓存；否则需要完整 users 集合。
- **错误：** 按 Fence 数排序释放未提交区间。**处理：** RecordingOwned 先阻止复用；实际提交后才能分配 Fence。
- **错误：** bindless CPU freelist 延迟了，但原 index 上 Descriptor 仍可被覆盖。**处理：** Rename-on-update，旧槽保留到安全完成。
- **错误：** Ring 达上限时分配一个新的 Shader-visible heap。**处理：** 当前架构 Root Table 绑定状态未准备动态切换，禁止此 fallback；改为显式失败/受控限流。
- **错误：** 空间耗尽在 `_mainheap_mutex` 内等待 Fence，GPU 提交线程又需要该锁。**处理：** 失败快速返回，等待仅在锁外、确认无人依赖当前录制后执行。

### M1-G. 交付分片与审查门槛

- **T1.1：** 完成调用点审计 + DESC-01/02 小模型；无生产修改。
- **T1.2：** Dynamic Ring reservation + owner + retire；DESC-01/02/03/04/07/08 通过；独立编译与提交。
- **T1.3：** Bindless deferred release 与 rename；DESC-05/06 和真实 Buffer/Texture 验证通过；独立编译与提交。
- **T1.4：** Debug/Release 渲染、压力与性能回归。记录 `reservations_active`, `reservations_submitted`, `reservations_retired`, `ring_overflow`, `bindless_pending` 的峰值；无新 GPU Validation 错误。

验收不取决于作者声称“加了 Fence”，而取决于：**在任何时刻，对 GPU 尚可能读取的 Descriptor 槽进行 CPU 重写都被阻止**。若无法证明所有裸 GPU Handle 的真实 owner，M1 维持 BLOCKED。


## Acceptance Criteria

- [ ] 每种 Descriptor GPU 使用调用点均已标记并纳入协议；不能留下隐藏裸 GPU Handle 的悬挂生存期。
- [ ] Ring 回绕、乱序录制/提交、多帧 in-flight、Bindless 更新/释放用例通过。
- [ ] 在 Debug Layer/GBV 下无 Descriptor heap 篡改/绑定相关错误，无无限等待与死锁。
- [ ] CapacityExceeded 不静默覆盖；正常容量压力测试不发生资源错误。
- [ ] Profile：记录 copy/cache/alloc 次数、CPU/GPU 与 baseline 对比；明显回退需解释和修正。
- [ ] 独立提交并在 `progress.md` 留有可重复验证命令、结果后才能进入 M2。

## Decision Log / Surprises / Validation / Progress / Known Follow-ups / Outcomes / Resume Protocol

- Decisions: 「局部缓存」或「跨列表共享+引用计数」只能选其一，先用调用链和基准确定最小方案，写入 progress。
- Discoveries: 跟踪任何隐式动态 GPU Handle 获取行为；不要遗漏纹理直接接口。
- Validation: M0 Descriptor 压测全矩阵 + Editor 测试 + Debug Layer。
- Progress: TODO；Next: 先做 Step 1 调用链清单。
- Known follow-ups: 多 Heap 分块/全局 Descriptor residency 属于后续优化。
- Outcomes: 未实施。Resume: README + progress + 本文件。
