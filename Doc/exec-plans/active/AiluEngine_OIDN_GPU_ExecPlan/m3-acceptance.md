# M3 implementation / acceptance

M3 代码已实现。真实 GPU plumbing smoke 已通过；完整 Editor 场景画质、Compute/DXR 对照和 1080p 性能验收仍待执行。

## 使用

```powershell
cmake --preset debug -DAILU_WITH_OIDN_GPU=ON
cmake --build --preset build-debug
```

`AILU_OIDN_RUNTIME_DIRECTORY` 指向 OIDN 2.5.1 的绝对 bin 目录，默认是已部署的
`.artifacts/out/oidn-2.5.1/package/oidn-2.5.1.x64.windows/bin`。没有 SDK headers/import library 依赖。
OFF 配置不初始化 OIDN，不支持/缺库时显示 Reference 原图。

在 Editor 打开 PathTracing，设置 RayTraceGI：

- `_accumulation_mode = kReference`
- `_denoiser = kOidnGpu`
- `_denoiser_quality = kBalanced`（也支持 Fast/High）
- `_denoiser_sample_interval = 32`（64 SPP 后的最小间隔）
- `_debug_temporal_target = 0`

静止相机后观察诊断状态。初始化期间、没有结果或出错时显示当前 Reference History；处理中保留同代已完成预览。
先尝试 4/8/16/32/64 SPP 阈值，之后按 interval 节流。首次 SDK 初始化或繁忙请求可能错过低 SPP 阈值，源 SPP 显示实际 snapshot 的计数。
Debug1～5 优先级保持不变。Realtime/Disabled 不使用 OIDN，原 Reference History 不接收降噪输出。
Features 调试面板显示状态/错误、已发布源 SPP、端到端 latency 和当前 shared-buffer MB。
这些 MB 不包含 SDK scratch、preview textures 和框架延迟释放的资源；latency 不等于纯 OIDN GPU ms。

## 当前证据

2026-10-14，Windows x64 / MSVC 19.51 / Windows SDK 10.0.26100，RTX 5070，驱动 32.0.16.1088。
OIDN 2.5.1 CUDA，LUID `0:64880`，nodeMask=1，externalMemoryTypes=1073742844，externalSemaphoreTypes=382。

- ON Debug 构建与 shader compile：通过（最终增量结果同步在 progress.md）。
- M1 contract / synthetic RG compile smoke：通过。
- DX12 shared resource/fence 创建、导出、关闭：32 次通过。
- 新 PackHDR / UnpackHDR / ClearInvalidCounter 的 SM5.1 编译：通过。
- `OidnGpuRenderGraphSmoke.exe`：100 次实际 CUDA filter → RG Unpack → GPU Copy 消费/发布通过，正常退出。
- 测试包括 63×33 → 127×65、Balanced → High、每 10 次结果 None/OIDN 切换；request ID 保持单调。
- Shared allocations 从 131072B 到 393216B；最后一次端到端发布 latency 24.0312ms。此值只适用于该小尺寸隔离测试，不外推到 1080p。
- 此次测试日志未发现 D3D12 resource-state/barrier 错误。临时空工程初次启动的 AssetDatabase 缺失日志和框架基础 Type allocation leak 输出单独保留，未当作 OIDN GPU 告警。
- 没有 HDR Map/Readback/CPU upload 的实现路径。测试用 GPU clear 写入 HDR `(2, .5, .25)`；没有 CPU 读取 filter output。

证据目录：
`F:/AiluEngine/artifacts/out/test/bin/x64/debug/OidnRG_20261014_09/`
（stdout.log / stderr.log / FileAppender_log.txt）。测试保存于独立工程，不修改用户场景。

复现 GPU smoke：

```powershell
& 'F:\AiluEngine\artifacts\out\test\bin\x64\debug\OidnGpuRenderGraphSmoke.exe'
```

默认创建唯一测试目录；也可以传入新的绝对目录。已有同名测试工程/config 时拒绝覆盖。
这个 smoke 使用生产 Host/backend、真实 RG 和 queue submit，但使用合成输入和 GPU Copy consumer；不是 GIPass 积分器或实际 Debug GI 图像验收。

## 同步与失效

RHI 在 `_command_submit_mtx` 下 ExecuteCommandLists 后发出每个 Pack 的 shared ready Signal；post-submit 仅排队后台 OIDN wait/filter/signal。
CPU 非阻塞查询 done；Unpack 带一个已完成的外部 fence Wait 作为跨 API acquire，不提前阻塞图形队列。
只有 Unpack 的 engine submission fence 完成后才发布独立 RGBA32F preview，并在当前图重新 Import。
两张 preview 轮换；共享 slot 等 Unpack 完成再复用，默认最多一个请求。

Generation/source identity/dimensions/SPP 保护发布。相机/history/shader revision、质量、功能启停和 render gap 使旧预览失效；
resize/quality/disable 将旧 Session 交给 context 持有，完成 native work 后在后台 sync/release，渲染路径不等待。
Context 受控退出在 descriptor/registry 销毁前 drain session；所有 worker 只持有 COM/session snapshots。

Pack 将负值和 NaN/Inf 分量替换为 0，保留有限正 HDR。无效像素计数保存在 input buffer 的
`width * height * 16` 偏移处（GPU u32，之后 12B 保留）；可用 PIX 检查，没有读回完整 HDR 或该 counter。

## 用户仍需验收

固定场景的 Compute/DXR Reference 4/8/16/32 SPP 画质、>1 HDR/颜色一致性、Debug1～5、运动/场景切换/热载、720p↔1080p、
缺 DLL/不支持设备/导入失败、PIX 与正常 frame time/SDK scratch memory。每组恢复原相机和 runtime 属性，不保存测试场景修改。
捕获按 `.agents/skills/ailu-render-frame-validation/SKILL.md`，证据放在实际 Editor.exe 旁的独立目录。

M3 acceptance 保持 pending；M4 未实施。

## 测试修复记录

隔离 harness 的启动/提交前提曾导致失败：完整 Renderer 冷编译耗时、GPU profiler 未初始化、空 ImGui frame 未完成，均已避免或补齐。
早期没有 Present 的 harness 只积累命令，不能当作提交或 GPU 成功；这些失败日志也保留。
首次测试覆盖的仓库根 FileAppender_log.txt 已从本轮开始时的 Git 干净版本恢复；测试日志另存
`.artifacts/out/oidn-2.5.1/m3-first-test-FileAppender_log.txt`，恢复前校验两份 SHA256 相同。

最终复核：ON 完整 Debug 构建及 OFF Engine/contract/spike 回归通过。最终复测 Ready=100、request=110、source SPP=92，退出码 0；contract/shader smoke 同时通过。当前 Debug cache 保持 ON，默认属性 kNone。
