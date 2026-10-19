# M1 / M2 自行验收入口

本文件保留 M1/M2 阶段的验收步骤。M3 已接入正式 Reference OIDN GPU 预览，并通过真实 GPU RG smoke；当前使用与证据见 [m3-acceptance.md](m3-acceptance.md)。完整场景画面/性能验收仍待执行。

## 构建与现有证据

- Windows x64 / VS2026 / MSVC 19.51 / Windows SDK 10.0.26100。
- `cmake --build --preset build-debug`：OFF 完整构建通过。
- ON 分支的 `OidnGpuInteropSpike`：编译通过；`--shader-only` 的 Pack/Unpack HLSL 离线编译通过。
- OFF `OidnGpuInteropSpike --disabled`：通过，不初始化 GPU。
- `PathTracingDenoiserSmoke.exe`：合约与合成 HDR RG producer/consumer 编译通过，不等于 GPU 执行或画面验收。
- 安装脚本已将官方 OIDN 2.5.1 Windows 包下载、SHA256 验证并解压到工作区 `.artifacts/out/oidn-2.5.1/package/`。
- 默认 cache 恢复为 `AILU_WITH_OIDN_GPU=OFF`；没有启动 Editor 或滤波窗口。

## M1

```powershell
cmake --preset debug -DAILU_WITH_OIDN_GPU=OFF
cmake --build --preset build-debug
& 'F:\AiluEngine\artifacts\out\test\bin\x64\debug\PathTracingDenoiserSmoke.exe'
& 'F:\AiluEngine\artifacts\out\test\bin\x64\debug\OidnGpuInteropSpike.exe' --disabled
```

Editor 中设置 RayTraceGI 的 `_denoiser`：None / OidnGpu。只有 Reference 模式会报告 OIDN Unsupported，日志说明 M3 尚未接入；其他累积模式不自动切换。
固定场景与相机检查 None / Unsupported 的原图、Debug1～5、Resize、相机 cut、场景切换、shader reload。None 不创建共享资源或 SDK 设备。
合成 backend 仅位于 `PathTracingDenoiserSmoke.cpp`，默认宿主没有安装该工厂；smoke 检查图依赖，不执行 GPU clear，也未证明通过实际 Debug GI 显示。

## M2

工作区已准备好 ON 版独立程序，首次验收可以直接运行，无需重新构建：

```powershell
$oidn_bin = (Resolve-Path '.artifacts/out/oidn-2.5.1/package/oidn-2.5.1.x64.windows/bin').Path
$spike = (Resolve-Path '.artifacts/out/oidn-2.5.1/OidnGpuInteropSpike.exe').Path
& $spike --shared-only 1023 513
& $spike $oidn_bin 1023 513 32
```

这份独立 ON executable 是当前实现的构建副本，不受随后恢复 OFF 配置影响；源码再修改后需按以下方法重新构建。

无需 CUDA SDK 或 OIDN import library。可选运行库按绝对目录动态加载，版本必须 2.5.1。
默认安装目录已经存在；需重装或其他机器部署时运行：

```powershell
$oidn_bin = & .\Test\InstallOidnGpuRuntime.ps1
cmake --preset debug -DAILU_WITH_OIDN_GPU=ON
cmake --build --preset build-debug --target OidnGpuInteropSpike
$spike = 'F:\AiluEngine\artifacts\out\test\bin\x64\debug\OidnGpuInteropSpike.exe'
& $spike --shared-only 1023 513
& $spike $oidn_bin 1023 513 32
& $spike $oidn_bin 1920 1080 32
```

每次滤波测试运行结束后窗口会保留最后一帧，关闭窗口后再运行下一条。左半图是 noisy HDR 的显示转换，右半图是 OIDN 输出；每个 request 的输入色阶和噪声改变。
另一个名为 `OIDN Linear HDR Result (alpha=1)` 的 RGBA32F GPU texture 保留完整线性输出，供 PIX/RenderDoc 检查，没有读回。显示转换只应用于 swapchain 预览。

检查控制台的 adapter/driver/LUID/nodeMask、OIDN name/version、memory/semaphore bitfield、logical/shared allocation bytes、request ID、fence latency。
`fence_latency_ms` 是包含 CPU 排队、滤波、轮询和呈现的端到端延迟，不能当成 OIDN GPU ms；SDK scratch memory 和 pack/unpack GPU ms 需用 PIX 等另测。
观察 DX12 debug layer，确认没有非法 barrier、越界访问、损坏或 lifetime 告警。HDR_CPU_bytes=0 是此实现无传输路径的声明，仍需 capture 核实。

建议补充：更小/更大及奇数尺寸，连续请求，在 Filtering 时关窗，重复启动，错误 DLL 目录、不支持 GPU、同卡身份/外部类型缺失和导入失败。
不支持/初始化失败退出码为 2，运行失败为 1；正常退出为 0（提前关窗不会宣称完成所有 requests）。缺设备时不会选 CPU。
当前没有故障注入开关；类型缺失和 import 失败分支需要对应设备环境或调试器验证。
窗口尺寸固定；尺寸重建可通过不同尺寸的独立运行检查。生产帧中的动态 Resize/Retirement 是 M3 范围。

验收后恢复默认配置：

```powershell
cmake --preset debug -DAILU_WITH_OIDN_GPU=OFF
cmake --build --preset build-debug
```

## 同步与 M3 接入限制

M2 的 `SubmitPack` 原子工作单元是独占队列上的 ExecuteCommandLists → ready Signal；OIDN 在同一 device 上依次 wait → filter async → done signal。
CPU 仅查询 done fence，完成后录制 Unpack；consume fence 未完成时不能重用 slot/command allocator。
关闭时先 sync OIDN、再 drain DX12，随后释放 filter/imports/device/HANDLE/resources/DLL；异常退出路径也先 drain 再销毁命令资源。
这是独立程序的受控关闭协议，不能直接放进正常 Editor frame 的 disable/resize 流程。

M3 必须将外部 signal 放在 `D3DContext::ExecuteRHICommandBuffers` 的 `_command_submit_mtx` 内、实际 Pack batch 提交之后，并用独立 job owner 保持 GPU resources 存活。
不得用 RG execute lambda、SPP 计数或普通 post-submit callback 代替资源 handoff 与 fence 完成证明。
M3 的正式 output publication、低频调度、多帧保留旧 preview、异步 retirement 尚未实现。

## 官方依赖依据

- [OIDN 2.5.1 API](https://github.com/RenderKit/oidn/blob/v2.5.1/include/OpenImageDenoise/oidn.h)
- [Windows release / SHA256](https://github.com/RenderKit/oidn/releases/expanded_assets/v2.5.1)
- [GPU interop 文档](https://www.openimagedenoise.org/documentation.html)
