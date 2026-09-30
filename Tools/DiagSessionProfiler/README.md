# DiagSessionProfiler

把 Visual Studio 诊断会话（`.diagsession`）变成**大模型可以直接消化的性能数据集**。

输入一个 `.diagsession`，输出三份文件：

| 文件 | 用途 |
|---|---|
| `report.md` | 自描述的性能数据集（环境、可信度、线程、模块/组件占比、源文件:行 热点、自耗/包含耗时 Top 表、调用链、时间线）。直接贴给大模型即可 |
| `profile.json` | 同样内容的机器可读版本，便于做 baseline / after 对比 |
| `prompt.md` | 一段可直接复制的提示词，要求模型输出「归因 → 优先级 → 改动位置 → 验证方法」 |

`report.md` 里所有百分比的分母、采样单位、符号来源分布、未解析比例都写在文件头部，模型不需要猜口径。

## 依赖

| 依赖 | 说明 |
|---|---|
| .NET 8 SDK | 构建与运行 |
| Windows Performance Toolkit（`xperf.exe`） | 解析 ETL 事件流；随 Windows ADK 安装 |
| LLVM 工具（`llvm-pdbutil.exe` / `llvm-symbolizer.exe`） | 项目自身代码的符号解析；随 Visual Studio 安装（`VC\Tools\Llvm\x64\bin`） |
| Windows 调试工具（`dbghelp.dll` + `symsrv.dll`） | 系统/第三方模块符号；随 Windows SDK 的 Debugging Tools 安装 |

工具会自动在 Windows Kits、Visual Studio（含 `vswhere`）、`PATH` 下探测这些可执行文件，也可以用 `--xperf` / `--llvm-pdbutil` / `--llvm-symbolizer` / `--dbghelp` 手动指定。

## 快速开始

```powershell
# 全流程
dotnet run --project Tools/DiagSessionProfiler -- all -i D:\profiles\opti_baseline.diagsession -o D:\profiles\baseline

# 或者用包装脚本（自动构建，自动挑选进程）
pwsh Tools/DiagSessionProfiler/Analyze-DiagSession.ps1 -Diagsession D:\profiles\opti_baseline.diagsession
```

跑完后把 `<输出目录>\report.md` 的内容连同 `prompt.md` 里的指令一起发给大模型。

### 常用参数

| 参数 | 说明 |
|---|---|
| `-i, --input <file>` | 输入 `.diagsession`（必填） |
| `-o, --output <dir>` | 输出目录，默认 `<输入目录>\<文件名>.profile` |
| `-c, --cache <dir>` | 缓存目录，默认 `<输出目录>\cache` |
| `-p, --process <name>` | 指定被剖析进程；默认按诊断包的「用户代码模块」列表自动识别 |
| `--top <n>` | 每张表保留多少行（默认 40，直接决定 token 量） |
| `--no-system-symbols` | 不下载 Windows 符号（离线场景） |
| `--deep-symbols` | 用模块符号（含 static 函数，实测 17.7 万条）而不是 publics 建函数索引；覆盖率略高，热缓存下多花约 30 s |
| `--command-lines` | 额外解析进程命令行（需要一次完整 XML 转储，较慢） |
| `--force` | 忽略缓存重新分析 |
| `--keep-intermediate` | 保留 `events.csv` 等中间产物 |
| `--symbol-path <path>` | 额外的 PDB 搜索路径（`;` 分隔，支持 `srv*cache*server` 语法） |

子命令：`extract`（只解包+解析事件）、`symbols`（只解析符号）、`report`（只出报告）、`all`（默认）。分阶段执行便于反复调参，例如只改了 `--top` 时重跑 `report` 是秒级的。

## 工作流程与缓存

```
① 解包       .diagsession -> package/（metadata.xml、*.counters、*.etl）
② 事件转储   xperf -a dumper -> events.csv（约 100 MB，之后删除）
③ 解析建模   -> cache/model/model.tsv（采样、栈、模块、线程、机器信息）
④ 项目符号   PDB 定位与校验 -> 打补丁镜像 -> llvm-symbolizer + 函数索引 -> cache/sym/*.tsv
⑤ 系统符号   dbghelp + symsrv -> cache/sym/*.ms.tsv，PDB 落盘 cache/mssym/
⑥⑦ 聚合报告  -> report.md / profile.json / prompt.md
```

所有阶段产物都带缓存：同一次构建重复分析时，②③④⑤ 全部命中缓存，只重算报告（秒级）。

## 设计要点

- **只用 `xperf` 的 CSV 转储，不解析 tracerpt 的 XML。** CSV 里同时包含 `SampledProfile`（采样）、`Stack`（栈帧）、`ImageId`（模块基址/大小/时间戳）、`DbgId/RSDS`（每个模块的 PDB GUID + age）、`ThreadName`、`BuildInfo`、磁盘型号等，比 XML 更全而且小一个数量级（XML 还会丢掉栈帧）。
- **符号必须校验，不能静默降级。** 追踪里记录了每个模块的 PDB GUID + age；工具会先定位候选 PDB、用 `llvm-pdbutil dump -summary` 核对 GUID/age，不一致就明确告警并放弃该模块（典型场景：剖析之后又构建了一次）。这一步是必需的——否则会拿到另一份构建的函数名，看起来合理但全错。
- **PDB 与镜像解耦。** 剖析当时的 PDB 常常只剩在 VS 的符号缓存里（`%TEMP%\SymbolCache\<名字>.pdb\<GUID><age>\`），而磁盘上的 DLL 已经被重新构建。工具会把镜像的 CodeView(RSDS) 记录改写成剖析当时的 GUID/age/PDB 名，再把缓存里的 PDB 硬链接到旁边，交给 `llvm-symbolizer` 解析——这样函数名和 `文件:行` 精确对应**当时那次运行**。
- **纯自耗 vs 包含耗时分开统计。** 自耗用全部采样（点采样本身就是叶子），包含耗时与调用链只对有栈采样，栈覆盖率写在报告头部，避免把两者混为一谈。
- **名称规范化。** C++ 模板名会折叠过长的模板参数、去掉参数列表，既省 token 又便于聚合（例如几十个 `tuple<...>` 变体会归并到同一条目）。

## 实测性能（同一台机器，25 MB 的 ETL / 6.3 万采样 / 61 个模块）

| 场景 | 耗时 | 说明 |
|---|---|---|
| 首次运行（冷缓存） | 4-6 min | 大头是首次从 msdl 下载约 60 个 PDB（约 200 MB） |
| `--deep-symbols`（热缓存） | 约 32 s | 额外构建模块符号索引（Engine.dll 17.7 万条、Editor.exe 8 万条） |
| 二次运行（模型与符号都命中缓存） | **约 3 s** | `report.md` / `profile.json` / `prompt.md` 全部重算 |
| 只调 `--top` 重出报告 | 约 3 s | 用 `report` 子命令 |

符号结果按 `模块_GUID+age` 缓存，未命中的模块也会写一条空缓存（负缓存），避免每次运行都去符号服务器上白找一遍。

## 输出可信度怎么读

`report.md` 第 1 节会明确给出：

- 采样间隔与「1 采样 ≈ 1 ms 单核 CPU」的换算
- 栈覆盖率（有栈采样 / 全部采样）
- 解析到函数名的采样比例、符号来源分布（`pdb` / `functions` / `publics` / `msdl` / `unresolved`）
- 每个模块的 PDB 身份与是否解析成功

如果某模块显示「无（显示为 模块+偏移）」，说明缺少匹配的 PDB，报告里该模块的热点只能到偏移粒度——这不是分析错误，而是数据边界。

## 常见问题

| 现象 | 处理 |
|---|---|
| `xperf.exe not found` | 安装 Windows ADK 的 Windows Performance Toolkit，或用 `--xperf` 指定 |
| 目标进程识别错 | 用 `-p Editor.exe` 显式指定；报告会说明识别依据 |
| 项目模块全是偏移 | 打开 `run.log` 看 PDB 定位/校验的告警；确认 VS 符号缓存里还有其他构建的 PDB，或用 `--symbol-path` 指向私有符号服务器 |
| 系统符号解析不到 | Debugging Tools 的 `dbghelp.dll` 旁边必须有 `symsrv.dll`；离线时用 `--no-system-symbols` |
| 报告太大/太小 | 调 `--top`（表行数）；时间线固定为 100 ms 桶 |
| 想对比优化前后 | 跑两次得到两份 `profile.json`，直接 diff 各表的 `samples` / `share` |
