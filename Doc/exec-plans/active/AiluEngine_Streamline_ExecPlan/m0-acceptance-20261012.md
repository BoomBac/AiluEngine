# M0 验收记录 · 2026-10-12

## 结论

**Blocked，未通过，不进入 M1。** 本次仅增加验收 driver 和文档，没有修改 runtime 实现。
原记录中的“未观察到 Present”已由实测替代，但首次 proxy Present 不等同于 common plugin 每帧命中。

## 环境与复现

- 源码 commit：`d428e9b2a1cd3f768ac29bdcac2c58e65724cd2c`，保留工作树原有 M0 dirty changes。
- executable：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe`。
- evidence：`F:/AiluEngine/artifacts/out/editor/bin/x64/debug/StreamlineM0Acceptance_20261012_2055/`。
- SDK 2.14.1，运行日志记录 interposer 2.14.1.0、`sl.common`/`sl.dlss` 2.14.1.98614dad6 PRODUCTION。
- RTX 5070；运行时实际开启 DX12 Debug Layer（不以 CMake 默认宏推断实际状态）。
- fixture：evidence 下 `clean-project/M0.ailuproject`，复制当前 `empty.almap`，含全局 PostprocessVolume 和 directional light。
- 固定场景只用于基础渲染检查；没有 mesh、材质复杂度或多 RenderWindow 验收。
- 隔离期间临时禁用 `Plugins/BlenderLiveLink/plugin.json` autoload；Editor 配置和 manifest 按字节备份/恢复。

## 门禁结果

| 门禁 | 本次结果 | 证据 / 限制 |
| --- | --- | --- |
| SDK-on build-debug | Pass | 首次构建 exit 0；恢复构建日志 `build-on-restored.log` |
| SDK-off build-debug | Pass | `build-off.log`、`build-off-result.txt`，exit 0 |
| SDK-off 无运行时 DLL | Pass（固定场景） | 暂移整个 Streamline 目录；`sdk-off/result.txt`，capture driver exit 0、实际 HWND WM_CLOSE、Editor exit 0 |
| SDK-on 缺 interposer DLL | Pass（固定场景） | `missing-dll/`；明确 unavailable/native DX12 日志、没有 proxy Present 标记、30 帧后 exit 0 |
| RTX capability | Pass | `loaded-clean/`：SDK loaded、plugin loaded、DLSS_SR supported；未执行 DLSS evaluate |
| 实际已使用的必需 hooks | 部分通过 | Device CreateCommandQueue、Factory CreateSwapChainForHwnd、SwapChain SetFullscreenState/GetBuffer/GetCurrentBackBufferIndex/Present/ResizeBuffers 走 proxy；其它必需 API 当前未使用 |
| Resize 后有效 TAA 输出 | Pass（固定场景） | SDK-on/off 两次 Win32 Resize 后均捕获连续 4 帧 |
| 三次受控启停 | Pass（隔离 fixture） | `loaded-clean`、`loaded-repeat-2`、`loaded-repeat-3` 均 exit 0 且记录 common plugin Shutdown |
| DX12 Debug Layer 新增严重错误 | 未观察到 | on/off 均无 severity 0/1；warning IDs 相同：1328、677、679、680、974。日志的 ERROR 标签不等于 D3D severity ERROR |
| common presentCommon 每帧 / Resize / 前后台 | Blocked | 未获得断点或逐帧计数；首次 proxy Present 和 RG 连续输出只属于间接证据；前后台切换未执行 |
| COM live-object 泄漏 | Blocked | 退出码 0 不证明没有 COM 泄漏；未执行 live-object 报告 |
| 非 RTX 真机回退 | Blocked | 当前只使用 RTX 5070；没有以模拟 capability 替代真实非 RTX 测试 |
| TAA/Postprocess 范围约束 | Pass（源码） | 现有实现改动集中于 build/runtime/DX12 hook 层；没有 TAA/Postprocess 逻辑改动 |
| 完整最终画面 / ImGui 视觉验收 | 未完成 | 分析的是 TAA 中间输出，不能替代后处理、UI/Present 合成或 live Editor 视觉验收 |

构建命令均使用 `cmake --build --preset build-debug`。
配置切换使用 `cmake --preset debug -DAILU_WITH_STREAMLINE=OFF`，恢复时使用
`cmake --preset debug -DAILU_WITH_STREAMLINE=ON -DAILU_STREAMLINE_SDK_ROOT=F:/AiluBuild/deps/Streamline/2.14.1`。

## 源码发现

1. **UpgradeDevice 失败的 COM ownership 不安全。** `StreamlineDx12Bridge.cpp:14` 先把 borrowed native device
   写入调用者 `_streamline_device_proxy.GetAddressOf()`，没有 AddRef。SDK upgrade 失败或保持 native 时，函数返回 false，
   但 owning ComPtr 仍非空；后续 Reset 会额外 Release native device。应只在成功取得独立 proxy 引用后提交输出，失败保持空。
   这是源码可确认的失败路径问题，本次没有注入 SDK upgrade failure，也不宣称已造成实测崩溃。
2. **非 hooked GetDesc 仍走 proxy。** `D3DSwapchain.cpp:130` 使用 `_swapchain->GetDesc`，没有使用已取得的
   `_native_swapchain`；native lookup 失败也只是 warning，未关闭该不完整路径。
3. **Present 验收证据不足。** `D3DSwapchain.cpp:81` 的 process-wide 首次日志只证明调用进入 Present，
   无法计数 common hook 是否持续执行、Resize 是否正确触发、多个 window 是否遗漏。

官方 2.14.1 指南要求 native 接口用于非 hooked API，并要求验证 common `presentCommon` 每帧和 Resize 周边执行：
[Manual Hooking v2.14.1](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideManualHooking.md)。
本次核对安装 SDK 源码 `source/core/sl.api/sl.cpp` 的 upgrade 失败行为与该版本文档。

## 捕获与分析

driver：`Test/StreamlineM0EditorAcceptance.mjs`，复用现有 RPC client 和 Win32 window control。
每次临时设置 scene camera AA=2（TAA），捕获完成后恢复原 AA/pose；不保存 scene。
driver 用法：`node Test/StreamlineM0EditorAcceptance.mjs <project-root> <unique-evidence-directory>`，需已运行的隔离 Editor。
它验证 TAA 和窗口 Resize，不负责 SDK hook 断点或进程启停。

| 输出 | SceneView 尺寸 | SDK-on engine_frame | SDK-off 帧连续 | 代表帧 on/off RMS（8-bit RGB） |
| --- | --- | --- | --- | --- |
| Resize 前 | 896×512 | 129–132 | 是 | 0.299 |
| Win32 1280×720 后 | 768×384 | 314–317 | 是 | 0.757 |
| Win32 1600×900 后 | 960×512 | 449–452 | 是 | 0.541 |

两种配置各 12 帧，共 24 帧。各组 count=4、frame_interval=1、无 gap；不同组之间不是连续采样。
SDK-on 三组的 mean 约 99.75–100.22/255，非黑像素比例 100%；组内相邻 RMS 0.520–1.441。
SDK-off 代表帧非黑比例 100%。代表图像已查看，显示正常天空/太阳/地平线。
PNG 是显示输出，不能证明源 HDR 没有 NaN/Inf；天空动画、独立启动和 TAA jitter 也限制了逐像素对照含义。
没有把 RMS 设置为任意全场景质量阈值。
详见 `loaded-resize/image-analysis.json` 与 `acceptance-summary.json`；原始 PNG 和 `capture.json` 保留于各 session。

## 失败尝试与恢复

- `loaded-1/`：未隔离 Live Link 的 120 帧运行曾接收 Blender 导入，退出码 0xC0000005。
  `crash.log` 指向 `ResourceMgr::ReleaseRuntimeResources -> Asset::~Asset -> shared_ptr::_Decref`。
  隔离后不复现；本次未证明具体根因，也没有修改资产/runtime 代码。
- `loaded-resize/`：capture driver exit 0，但对隐藏窗口使用 `Process.CloseMainWindow()` 后 15 秒未退出，测试进程被结束。
  该次不计作正常销毁证据；其 capture 仍可用于图像分析。SDK-off 改用实际 HWND 的 WM_CLOSE 已正常退出。
- 没有结束用户已有进程；测试结束检查 Editor 进程、临时 DLL 名和配置 diff。
- 当前保存的 M0 仍未通过；后续先修源码失败路径，再补直接 hook 和 leak 验证，不推进 M1。
