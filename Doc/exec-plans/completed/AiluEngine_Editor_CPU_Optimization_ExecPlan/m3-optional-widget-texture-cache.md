# M3 — Optional Widget Texture Cache

> 先决 Gate：M2 已通过，且 M0–M2 后的 Profile 证明静态面板的 Draw Submission 仍有可观 CPU 开销。默认路径继续使用 Geometry Cache。**未有性能收益证据可将 M3 结项为 NOT JUSTIFIED，避免过度设计**。

## Purpose

给少数高成本静态 Widget 增加可选的跨帧持久化 RenderTexture 缓存：只有绘制数据/视觉依赖发生变化时更新 RT，其他帧仅提交合成 Quad，减少每帧大量 Material/Scissor/DrawIndexed 命令的 CPU 录制。

## Scope

- 在现有 Geometry Cache 之上追加 **Widget 粒度** Texture Cache（opt-in、默认关闭）。
- 在 Renderer 准备阶段识别 RT Dirty，在安全的录制阶段先更新缓存内容，随后按原始 Z-order 合成到窗口 UI Composite。
- 处理 Window/Dock 尺寸变化、Popup 前后顺序、透明背景、裁剪、字体、主题、动态纹理及 RenderGraph/并行录制资源生命周期。
- 对复杂静态 Inspector/WorldOutline/AssetBrowser（先一两个试点）评估 CPU P50/P95 与显存成本，能切回 Geometry。

## Non-goals

- 不为每个 UIElement 创建 RT，也不要求所有 Widget 都使用 Texture Cache。
- 不实现任意子矩形 Dirty Rect 更新、精细缓存图集、跨窗口内容搬迁无重建、ImGui UI 的 RTT 化。
- 不改变 SceneView 实时 Scene Color 的生成、GameView 实时数据、对外 DX12 Texture 状态管理模型。
- 不重复实现 Paint Dirty/DrawNode/Frame GPU Buffer；RT 仅是现有几何缓存上的可选合成层。

## Architecture Constraints

1. `kGeometry` 保持**既有渲染行为**；`kTexture` 必须显式开启，不能简单依据 Cache Hit 自动强行缓存所有 Widget。
2. 复杂动态内容的第一版只能 Geometry/Direct；Texture Cache 资格可保守限制为 `static_only`、无 BackdropBlur、无动态外部 RT，资源依赖无法证明不变时 fallback。
3. 缓存图像存储**透明背景**，最终按原 Widget 顺序 Alpha Blend；确认现有 UI shader 采用 Straight Alpha 还是 Premultiplied Alpha，并保持一致。严禁在屏幕上直接无条件 `CopyTextureRegion` 覆盖透明像素。
4. 当前 DrawNode 顶点使用窗口坐标，`SubmitBlock()` 投影以目标 RT 尺寸建立，Scissor 也是目标区域坐标：离屏渲染时**必须显式转换**局部 viewport 的投影与 Scissor，而非直接换目标纹理。
5. 为了简洁，首版移动/缩放/改变目标窗口一律失效并重建 RT；有验证后才考虑“只移动合成 Quad、不重建”优化。避免将旧窗口坐标的几何烘焙当作局部空间缓存。
6. 真 Dirty 的 RT Cache 重绘可复用现有 CPU Paint Cache、DrawerBlock、`SubmitBlock`；若几何未变但外部依赖变化，必须仍能重绘缓存 RT。
7. RT 需要长期持有，不能用每帧 `GetTempRT/ReleaseTempRT` 实现跨帧 Cache；释放/Resize 由 GPU fence 保证不早于 GPU 完成；记录持久 RT 内存。
8. 多帧槽位和并行录制必须保证：Prepare 的 Cache State 快照与 Record 实际执行相匹配；不可在 Record Job 读写会被主线程中途销毁/修改的 Widget/RT 状态。
9. sRGB/Linear/HDR 与半透明 Overlay 的颜色行为需精确保持；不要通过“不明显”作为忽略 Gamma 或 Alpha 错误的理由。

## Baseline

现有路径：

```text
RenderPipeline::Render
  ├─ UIRenderer::Prepare
  │    ├─ Widget PreUpdate/Update
  │    ├─ IsPaintCacheDirty -> optional CPU DrawerBlock rebuild
  │    └─ SyncWidgetFrameBlocks -> optional multi-frame GPU Buffer dirty
  └─ UIRenderer::Record  // may be parallel Job
       ├─ GetTempRT("UI_Composite_<window>") -> clear
       ├─ each widget SubmitBlock -> every cached DrawNode DrawIndexed
       ├─ overlay/popup/global/window blocks
       └─ Blit UI Composite -> WindowBackBuffer
```

- `Widget::BindOutput` 允许 RT 目标切换，但只能决定 Target，不能自动适配当前**以屏幕位置构建**的几何/Scissor。
- `UIRenderer::SubmitBlock()` 逐节点设置 Texture/Material/Scissor 并发 `DrawIndexed`；`DrawerBlock::SubmitVertexData()` GPU 非 dirty 时已跳过上传。
- `UIRenderer::Record()` 负责 Window BackBuffer 和 CompositeTarget；Texture Cache 合成必须嵌入这一现有顺序，不允许自己在错误的阶段后插入一层盖住 Popup。
- `UIRenderer::Prepare()` 在主路径执行，Record 可并行；`UIRenderer` 已有 `_submit_entries` 描述提交顺序。

## Target Outcome

- Opt-in 静态 Widget 的稳定帧不再录制其全部子 DrawNode，只录制小量合成 Draw Calls；Cache Dirty 时仍正确重建并绘制。
- 使用 Texture Cache 的 Inspector 等面板，无颜色、透明度、交叠、点击、裁剪、文本清晰度回归。
- 在客观 CPU 收益可覆盖合成成本且内存可接受的机器上启用；否则默认 Geometry，明确记录 NOT JUSTIFIED。

## Relevant Files

- `Engine/Inc/UI/Widget.h`, `Engine/Src/UI/Widget.cpp`
- `Engine/Inc/UI/UIRenderer.h`, `Engine/Src/UI/UIRenderer.cpp`
- `Engine/Inc/UI/DrawerBlock.h`, `Engine/Src/UI/DrawerBlock.cpp`（通常无需大改）
- `Engine/Src/Render/RenderPipeline.cpp`
- `Engine/Inc/Render/RenderTexture.h` 或项目当前真实纹理生命周期/RenderTarget API（执行者确认）
- `Engine/Src/Render/CommandBuffer.cpp` 及 DX12 RHI 状态/资源释放相关实现（除非必要不改）
- `Editor/Src/Widgets/ObjectDetail.cpp`, `WorldOutline.cpp`, `AssetBrowser.cpp`, `RenderView.cpp`
- 现有 UI shader：`Engine/Res/Shaders/hlsl/default_ui.alasset` 对应 Shader 实现/资源

## Implementation Steps

### 3.0 利润门槛：先决定是否值得实现

1. 从 M2 Profile 读取稳定帧 UI DrawNode Count、DrawCall、`UIRenderer::Record/SubmitBlock` CPU P50/P95 和整体 Frame P50/P95。
2. 选择 **1–2 个真实静态、DrawNode 数量高** 的 Widget 当试点。不要从控件数量少的 StatusBar 开始。
3. 如果大头在 `UpdateScenes`、ImGui 或 Present/Wait 而 UI Record 开销很低，记录 `NOT JUSTIFIED`，本阶段以完成性能评估作为 Gate，不做 RT 模块。
4. 若确有热点，继续 3.1–3.7。基线与试点选择记到 `progress.md`。

### 3.1 最小模式与缓存状态

建议只添加两种明确模式：

```cpp
enum class EWidgetCacheMode : u8
{
    kGeometry,  // 默认，完全维持现有路径
    kTexture    // 显式 opt-in，满足兼容条件才能启用
};
```

缓存运行时状态最少包含：`Ref<RenderTexture> _texture`、`Vector2u _size`、`u64 _built_paint_revision`、`u64 _resource_dependency_revision`（或等效触发），`bool _valid`、`u64 _last_used_frame`、`Window *` / output 归属；不要把这些放在每个 UIElement 上。

区分两个概念：

- **Geometry Dirty**：需要重新构建 CPU DrawNode / GPU Buffer（原有逻辑）。
- **Texture Dirty**：几何内容、尺寸、样式/字体、外部视觉依赖变化后要重绘 Texture；可能 Geometry 完全没变，只是重采样结果应变化。

初期不能可靠识别外部依赖时直接保守回退 Geometry，并记录理由。

### 3.2 在现有准备/录制边界建立 RT 工作列表

- Prepare 中：确定可 Texture Cache 的 Widget，验证尺寸、资源可用性、Dirty、缓存 RT 生命周期，形成仅包含本帧需要的数据的稳定提交描述。
- Record 中：首先对 Dirty TextureCache 录制 DrawNode 到离屏 RT；接着按原有 `_submit_entries` 顺序，将缓存的 RT 作为 Quad 与 Geometry Widget 混合合成。
- Popup 应继续按既有优先级处理；Global/UI Debug Overlay 的顺序必须可对照现有 `Record()`。
- 渲染线程快照与 Widget 生存期绑定：若 Record 仍持有资源/Widget，不能由 UI 线程提前销毁；对跨线程读写/队列复用做安全证明或小范围同步。
- 原有 Geometry 路径一字不动的目标优先；不要复制整套 `SubmitBlock` 实现。

### 3.3 窗口坐标转到局部 RT

**这是首版最重要的出图风险。**

- 对 Widget 内容区域 `rect = (x, y, width, height)` 创建大小为 `(width, height)` 的局部 RT。
- `SubmitBlock()` 中基于目标矩形而非一律 `(0,0,w,h)` 建立正交投影：局部 RT 绘制时在投影上扣除原始屏幕 `(x, y)` 偏移；不得修改所有 CPU 缓存顶点后再恢复。
- `Rect scissor` 同样扣除 `(x, y)` 并限制在 `0..width/height`；处理负数、亚像素 DPI、最大 RT 尺寸和可能的剪裁溢出。
- 合成时按原窗口内容 `(x, y, width, height)` 位置绘制；Popup/Overlay 的本地与全局剪裁仍同预期。
- 对字体 MSDF、UI shader 可能使用的 `_ScreenParams`、圆角半径、Screen-space UV/BackdropBlur 依赖逐项确认；如果 Shader 依赖**真实屏幕像素位置**而无法等价重现，首版排除该 Widget。
- 测试非 (0,0) 位置、负偏移、窗口跨屏移动、DPI/Resize，确保画面与鼠标命中不偏移。

### 3.4 RT 清除、颜色/Alpha 与混合

- 仅当 Texture Dirty 时，将 RT 清成透明 `(0,0,0,0)`，录制缓存绘制；**未脏不清除、不重绘**。
- 显式核对现有 UI shader 混合方式、RT 格式的 sRGB encode/decode、最终 Composite 的色彩空间；校验叠加透明 Panel 与 SceneView 输出不会发灰、亮边或暗边。
- 缓存中可能使用透明/半透明 Border、圆角抗锯齿、字体 MSDF；按相同 Blend 方程合成而不是直接 Copy 覆盖。
- 对不透明已认证区域未来可走 Copy/Blit 快速路径，但**不在首版实现**。

### 3.5 动态内容和可用性回退

以下 **默认不启用** Texture Cache：

- 直接承载实时 `RenderTexture` 的 SceneView/GameView、视频/动态图像、变更但 Texture 指针恒定的预览。
- BackdropBlur、读取已合成背景的特殊 Brush、随屏幕位置而变化的效果。
- 焦点光标、滚动惯性或频繁动画、频繁 Resize 的临时 Widget（除非数据证明仍值得）。
- 纹理依赖/缓存生命周期无法正确追踪的插件控件。

如果要把 SceneView 的静态 Toolbar 单独缓存，必须先自然形成可独立绘制的静态 Widget；**不要为了这一步大拆 SceneView 的动态主图树**。

### 3.6 生命周期、池化与预算

- 缓存 RT 使用持久 `RenderTexture`（项目已有资源创建/回收接口），不要每帧 Temp RT。创建以 Widget 实际像素尺寸为准，限制最大边长。
- Resize / Detach / Target Window Change / Destroy：失效或释放旧 RT；必须等 GPU fence 安全点实际回收，避免继续被在途命令使用。
- 对隐藏 Widget 设置合理缓存保留/回收策略：初版简单 LRU 或关闭时释放即可，**不提前上通用 atlas**。
- 记录缓存纹理数量、分辨率、估计 VRAM；比如单张 1000×700 RGBA8 约 2.67 MiB（计算量级，不代表引擎实际开销）。
- GPU barrier / Shader Resource State 需遵循现有 `CommandBuffer/RenderGraph` 规则；使用 PIX/DX12 Debug Layer 证实 RenderTarget -> SRV 与跨帧重新写 RT 状态有效。

### 3.7 Feature Flag 与对照采样

- 增加受控 Debug/UI 开关：Geometry vs Texture，可实时（安全切换时机）比较；关闭 Texture 应释放或保守延迟释放缓存资源。
- 增加每帧 Texture Cache Hit/Miss、Dirty RT Count、RT Repaint CPU 时间、合成 Draw Calls、Resident RT MB 统计。
- 同一个静态 Inspector/AssetBrowser，分别测：Geometry vs Texture，在空闲、滚动、文字变化、选中对象、拖动 Dock、Resize 和第二窗口下 P50/P95 CPU/GPU 时间。
- 记录 `DrawIndexed` 数量下降以及**整个 Frame CPU 是否真下降**；若只是 GPU RT 开销上升而 CPU 无收益，默认不启用。

## Validation

**图像/功能 Gate：**

- [ ] Texture 默认关闭，Geometry 原路径构建/回归与 M2 指标保持。
- [ ] 静态复杂 Widget 启用缓存后可正确显示：文字清晰、透明圆角正确、裁剪和阴影正确、没有层序问题。
- [ ] 点击/输入/Focus/Tab 切换、Popup Modal/Backdrop、滚动/拖拽/Resize、窗口移动与主题变化无旧帧残留。
- [ ] SceneView/GameView 动态内容未冻结；异步预览图/纹理资源更新不丢失。
- [ ] RT Destroy/Resize/多窗口/多帧并发下无 UAF、GPU Resource State Error、D3D12 Debug Layer Warning；PIX 证明 barrier 和输入输出状态正确。

**性能/资源 Gate：**

- [ ] 相同案例下 Texture Cache Hit 后其子 DrawNode 不再每帧录制，合成 Draw Call 取代之；`UIRecord/Submit` CPU P50/P95 对比量化。
- [ ] 启用试点 Texture Cache 的空闲 UI Submission CPU 有明确正收益，并且**整帧 CPU / GPU P95 没有不可接受回归**；记录数值和运行配置，不设置脱离基线的空泛百分比。
- [ ] RT 个数、峰值额外显存、Resize/Detach 资源释放得到量化验证。
- [ ] 缺少 GPU 环境时标为 BLOCKED，不得宣称 Pass。
- [ ] 如果 M3 第 3.0 步证明 ROI 不成立：明确记录 `NOT JUSTIFIED`、Profile 证据和“不落地 RT”决策，**可作为受控结项，而不能谎称 RT 已实现**。
- [ ] 将完整结果和最终模式默认值、Evidence path 写入 `progress.md`。

## Milestones

仅 M3。若 Gate 未过，保留 Geometry，修正或记录技术 Blocker；不可提交默认开启的不稳定实现。

## Current Milestone

进入本文件时应由 `progress.md` 指向 M3。

## Progress

动态进度统一写 `progress.md`。

## Decision Log

- 与原 Geometry Cache 并存，起始默认关闭；不支持读取背景的 Brush 与动态 RenderTexture。
- 坐标变换与 Blend 是正确性优先项，不通过则回退。

## Surprises & Discoveries

当前 UI Composite 的基础开销不会因为某一个 Widget 改成 Texture Cache 自动消失；应用于缓存测试结论时需要区分量级。

## Known Follow-ups

RT atlas、子矩形局部更新、拖动窗口不重建、跨窗口无损迁移、UI/Scene 分频更新由后续独立计划评估。

## Outcomes

待 M3 实际选择与验收；不能把预估 CPU 收益当作实验结果。

## Resume Protocol

只读 README / progress / 本文件；如 M2 记录不足以支撑 M3，先回 M2 完成基线；GPU Gate 不通过不得默默默认开启。
