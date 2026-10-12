# Blender Live Link progress integration

## Purpose

将现有编辑器进度系统接入 Blender Live Link 完整同步。用户继续自行交互验收。

## Scope / Non-goals

- FullSyncBegin 开始任务，接收/应用期间更新阶段和已应用包数，FullSyncEnd 完成或报告失败。
- 断连、队列溢出、重复 Begin、插件卸载均结束旧任务。
- 不修改 Blender 插件或网络协议；没有可靠总量时显示不确定进度。
- 不展示取消按钮；不把单次主线程 Scene/GPU 应用改为后台任务。
- 增量编辑不逐包弹出进度卡片。

## Architecture constraints

Engine 不依赖 Editor。Plugin API v1 的 IPluginHost 和 PluginContext 布局保持不变，通过独立可选
IPluginProgressHost 接口和 RTTI 查询提供能力；要求既有兼容 C++ 工具链。PluginManager 持有 Editor 提供的
callback，调用仅在 owner thread，异常不越过 DLL 边界。回调不持有插件对象或插件字符串。
Editor 回调按任务标题保存 handle；插件卸载先终结自己的任务。服务 shutdown 后 handle 更新仍安全。

## Relevant files

- Engine/Inc/Framework/Plugin/PluginApi.h、PluginManager.h
- Engine/Src/Framework/Plugin/PluginManager.cpp
- Editor/Src/EditorApp.cpp
- Plugins/BlenderLiveLink/Inc/BlenderLiveLinkPlugin.h、LiveLinkSession.h
- Plugins/BlenderLiveLink/Src/BlenderLiveLinkPlugin.cpp、LiveLinkSession.cpp
- Test/PluginManagerSmoke.cpp、Test/Plugins/LifecycleProbe/LifecycleProbe.cpp

## Current milestone

实现及构建/回归验证；真实 Blender/Editor 交互待用户验证。

## Progress

- [x] 确认 ownership、threading、完整同步生命周期。
- [x] 可选 host 进度桥及 Editor adapter。
- [x] Full Sync 生命周期、应用结果、断连/溢出/卸载收尾。
- [x] Begin 后让出当前帧；命令消费采用约 4ms 批次预算，单个 Apply 不可中断。
- [x] Debug 构建与回归检查。
- [ ] 用户真实交互验收。

## Decision Log

- 2026-10-10：使用可选侧接口，避免修改已有 v1 虚表、context 或升级所有 manifest。
- 全量同步使用一个不确定任务；已应用包数不代表对象数或整体百分比。
- 主线程每批次让帧，以便面板刷新；等待 Blender 材质烘焙/传输时任务保持 running。

## Surprises & Discoveries

- LiveLinkSession 原先只公开 ObjectUpsert 成功状态；增加每条 Apply 的结果以避免其他数据失败仍显示成功。
- 源码中的 Blender FullSyncBegin 已在材质/网格导出之前发送，不需要修改 add-on。
- 已验证的重复/旧 mesh revision 是已应用数据，保留跳过行为并返回成功，避免被进度统计误报失败。

## Validation

- `cmake --build --preset build-debug`：通过（exit 0），包含 Engine、Editor、AiluBlenderLiveLink DLL 和测试目标。
- `PluginManagerSmoke.exe`：通过（exit 0）。增加跨 DLL probe Begin/Running/Cancelled 顺序、计数及回调异常隔离检查；保留重复加载、错误线程、回滚、卸载顺序检查。
- `PluginRegistrationSmoke.exe`：通过（exit 0），确认现有注册 ownership/卸载行为未回归。
- `EditorProgressSmoke.exe`：通过（exit 0），并发、终态、保留策略、Shutdown 和 handle lifetime。
- 运行目录：`F:/AiluEngine/artifacts/out/test/bin/x64/debug/`，进程 PATH 临时添加 Debug Editor DLL 目录，运行后恢复。
- `git diff --check`：通过。
- 未启动 Editor 或操作用户的 Blender 场景；以上结果不等同真实 Blender 同步或视觉验收。

人工步骤：Full Sync 检查开始、阶段、完成；复杂材质烘焙等待期间卡片仍存在；同步中断连显示失败；
重新连接并同步创建新任务；插件卸载结束活动任务；增量修改不反复弹出卡片。

## Outcomes

实现、Debug 构建和独立回归检查完成。真实 Blender Full Sync、等待烘焙、断连及重新同步交互待用户验收。
复用已有圆角/背景模糊面板，Blender add-on 无需重新打包。单个同步 Apply 仍可能阻塞当前帧。
