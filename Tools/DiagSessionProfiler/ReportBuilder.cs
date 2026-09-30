using System.Globalization;
using System.Text;
using System.Text.Json;

namespace Ailu.DiagSessionProfiler;

/// <summary>Stages 6-7: aggregate the trace and emit an LLM readable dataset.</summary>
public static class ReportBuilder
{
    private sealed class Frame
    {
        public string Module = "";
        public string ModuleDisplay = "";
        public ulong Offset;
        public string? Function;
        public string? File;
        public int Line;
        public string Source = "";
        public bool IsProject;
        public string Display => Function ?? $"{ModuleDisplay}+0x{Offset:x}";
        public string Location => File is null ? "" : $"{Path.GetFileName(File)}:{Line}";
    }

    private sealed class Aggregate
    {
        public long Samples;
        public string Location = "";
        public string Function = "";
        public string Module = "";
        public int Threads;
        public readonly HashSet<int> ThreadSet = [];
        public readonly Dictionary<string, long> Locations = new(StringComparer.Ordinal);

        public void AddLocation(string location)
        {
            if (location.Length == 0)
            {
                return;
            }
            Locations[location] = Locations.GetValueOrDefault(location) + 1;
        }

        public string HottestLocation()
        {
            if (Locations.Count == 0)
            {
                return "";
            }
            var best = "";
            long bestCount = -1;
            foreach (var pair in Locations)
            {
                if (pair.Value > bestCount)
                {
                    bestCount = pair.Value;
                    best = pair.Key;
                }
            }
            return best;
        }
    }

    public static void Build(Options options, PackageInfo package, TraceModel model, SymbolTable symbols, Logger log)
    {
        var projectModules = model.Modules.Values
            .Where(m => m.Pid == model.TargetPid && symbols.HasLocalSymbols(m.Name))
            .Select(m => m.Name.ToLowerInvariant())
            .ToHashSet();

        var selfByFunction = new Dictionary<string, Aggregate>(StringComparer.Ordinal);
        var selfByFile = new Dictionary<string, Aggregate>(StringComparer.Ordinal);
        var selfByFileLine = new Dictionary<string, Aggregate>(StringComparer.Ordinal);
        var selfByModule = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
        var selfByComponent = new Dictionary<string, long>(StringComparer.Ordinal);
        var selfByThread = new Dictionary<int, long>();
        var symbolSources = new Dictionary<string, long>(StringComparer.Ordinal);
        var timeline = new Dictionary<long, long>();
        long resolved = 0;

        Frame Resolve(int pid, long address)
        {
            var module = model.FindModule(pid, (ulong)address);
            if (module is null)
            {
                return new Frame { Module = "", ModuleDisplay = $"(unknown 0x{address:x})" };
            }
            var offset = (ulong)address - module.Base;
            var entry = symbols.Lookup(module.Name, offset);
            var frame = new Frame
            {
                Module = module.Name.ToLowerInvariant(),
                ModuleDisplay = module.Name,
                Offset = offset,
                Function = entry?.Function,
                File = entry?.File,
                Line = entry?.Line ?? 0,
                Source = entry?.Source ?? "",
                IsProject = projectModules.Contains(module.Name.ToLowerInvariant()),
            };
            return frame;
        }

        foreach (var sample in model.Samples)
        {
            var frame = Resolve(sample.Pid, sample.Address);
            var component = Classify(frame, model);
            Increment(selfByComponent, component);
            Increment(selfByModule, frame.ModuleDisplay.Length > 0 ? frame.ModuleDisplay : "(unknown)");
            Increment(timeline, (long)(sample.Time * 10)); // 100 ms buckets
            IncrementLong(selfByThread, sample.Tid);
            if (frame.Function is not null)
            {
                resolved++;
            }
            symbolSources[frame.Source.Length == 0 ? "unresolved" : frame.Source] = symbolSources.GetValueOrDefault(frame.Source.Length == 0 ? "unresolved" : frame.Source) + 1;

            var key = frame.Function ?? frame.ModuleDisplay + "+0x" + frame.Offset.ToString("x");
            Add(selfByFunction, key, frame, sample.Tid, model);
            if (frame.File is not null)
            {
                var file = Path.GetFileName(frame.File);
                Add(selfByFile, file, frame, sample.Tid, model);
                Add(selfByFileLine, $"{file}:{frame.Line}", frame, sample.Tid, model);
            }
        }

        var inclusiveByFunction = new Dictionary<string, Aggregate>(StringComparer.Ordinal);
        var chains = new Dictionary<string, long>(StringComparer.Ordinal);
        var inclusiveByModule = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
        foreach (var stack in model.Stacks)
        {
            var frames = new Frame[stack.Frames.Length];
            for (var i = 0; i < stack.Frames.Length; i++)
            {
                frames[i] = Resolve(stack.Pid, stack.Frames[i]);
            }
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var frame in frames)
            {
                var key = frame.Function ?? frame.ModuleDisplay + "+0x" + frame.Offset.ToString("x");
                if (seen.Add(key))
                {
                    Add(inclusiveByFunction, key, frame, stack.Tid, model);
                }
                Increment(inclusiveByModule, frame.ModuleDisplay.Length > 0 ? frame.ModuleDisplay : "(unknown)");
            }
            var chain = new StringBuilder();
            for (var i = 0; i < frames.Length && i < 8; i++)
            {
                if (i > 0)
                {
                    chain.Append(" < ");
                }
                chain.Append(frames[i].Function ?? frames[i].ModuleDisplay + "+0x" + frames[i].Offset.ToString("x"));
            }
            if (chain.Length > 0)
            {
                Increment(chains, chain.ToString());
            }
        }

        var statistics = new ReportStatistics
        {
            SelfByFunction = selfByFunction,
            SelfByFile = selfByFile,
            SelfByFileLine = selfByFileLine,
            SelfByModule = selfByModule,
            SelfByComponent = selfByComponent,
            SelfByThread = selfByThread,
            SymbolSources = symbolSources,
            InclusiveByFunction = inclusiveByFunction,
            InclusiveByModule = inclusiveByModule,
            Chains = chains,
            Timeline = timeline,
            ResolvedSamples = resolved,
        };

        var markdown = WriteMarkdown(options, package, model, statistics, symbols, projectModules);
        var reportPath = Path.Combine(options.OutputFull, "report.md");
        File.WriteAllText(reportPath, markdown, new UTF8Encoding(false));
        log.Info($"report.md written ({ExternalTool.Bytes(new FileInfo(reportPath).Length)}, ~{markdown.Length / 4:N0} tokens)");

        var jsonPath = Path.Combine(options.OutputFull, "profile.json");
        File.WriteAllText(jsonPath, WriteJson(options, package, model, statistics), new UTF8Encoding(false));
        log.Info($"profile.json written ({ExternalTool.Bytes(new FileInfo(jsonPath).Length)})");

        var promptPath = Path.Combine(options.OutputFull, "prompt.md");
        File.WriteAllText(promptPath, WritePrompt(model), new UTF8Encoding(false));
        log.Info($"prompt.md written");
    }

    private sealed class ReportStatistics
    {
        public Dictionary<string, Aggregate> SelfByFunction = [];
        public Dictionary<string, Aggregate> SelfByFile = [];
        public Dictionary<string, Aggregate> SelfByFileLine = [];
        public Dictionary<string, long> SelfByModule = [];
        public Dictionary<string, long> SelfByComponent = [];
        public Dictionary<int, long> SelfByThread = [];
        public Dictionary<string, long> SymbolSources = [];
        public Dictionary<string, Aggregate> InclusiveByFunction = [];
        public Dictionary<string, long> InclusiveByModule = [];
        public Dictionary<string, long> Chains = [];
        public Dictionary<long, long> Timeline = [];
        public long ResolvedSamples;
    }

    private static void Add(Dictionary<string, Aggregate> map, string key, Frame frame, int tid, TraceModel model)
    {
        if (!map.TryGetValue(key, out var aggregate))
        {
            aggregate = new Aggregate
            {
                Location = frame.Location,
                Function = frame.Function ?? "",
                Module = frame.ModuleDisplay,
            };
            map[key] = aggregate;
        }
        aggregate.Samples++;
        aggregate.ThreadSet.Add(tid);
        aggregate.Threads = aggregate.ThreadSet.Count;
        aggregate.AddLocation(frame.Location);
        _ = model;
    }

    private static string Classify(Frame frame, TraceModel model)
    {
        if (frame.Function is null)
        {
            return "unresolved";
        }
        if (frame.IsProject || frame.Source is "pdb" or "functions" or "publics")
        {
            if (frame.File is not null && NameShortener.IsToolchainFile(frame.File))
            {
                return "stdlib-cxx";
            }
            return frame.IsProject ? "project-code" : "third-party-code";
        }
        var module = frame.Module;
        if (module.Contains("nvwgf", StringComparison.Ordinal) || module.Contains("nvlddm", StringComparison.Ordinal) || module.Contains("atio", StringComparison.Ordinal)
            || module.Contains("amdvlk", StringComparison.Ordinal) || module.Contains("igvk", StringComparison.Ordinal) || module.Contains("igd", StringComparison.Ordinal))
        {
            return "gpu-driver";
        }
        if (module.StartsWith("ntdll") || module.StartsWith("ntoskrnl") || module.StartsWith("win32k") || module.StartsWith("kernel") || module.StartsWith("ucrtbase")
            || module.StartsWith("vcruntime") || module.StartsWith("msvcp") || module.StartsWith("user32") || module.StartsWith("win32u") || module.StartsWith("gdi32")
            || module.StartsWith("dxgi") || module.StartsWith("d3d") || module.StartsWith("dxgkrnl") || module.StartsWith("dxgmms"))
        {
            return "windows-runtime";
        }
        return "other-module";
    }

    private static void Increment(Dictionary<string, long> map, string key) => map[key] = map.GetValueOrDefault(key) + 1;

    private static void Increment(Dictionary<long, long> map, long key) => map[key] = map.GetValueOrDefault(key) + 1;

    private static void IncrementLong(Dictionary<int, long> map, int key) => map[key] = map.GetValueOrDefault(key) + 1;

    private static string WriteMarkdown(Options options, PackageInfo package, TraceModel model, ReportStatistics stats, SymbolTable symbols, HashSet<string> projectModules)
    {
        var total = Math.Max(1, model.TargetSamples);
        var builder = new StringBuilder();

        builder.AppendLine($"# 性能剖析数据集：{model.TargetProcessName} (pid {model.TargetPid})");
        builder.AppendLine();
        builder.AppendLine("> 由 DiagSessionProfiler 自动生成，供大语言模型做性能分析。所有百分比的分母是「目标进程采样总数」。");
        builder.AppendLine();

        builder.AppendLine("## 1. 数据来源与可信度");
        builder.AppendLine();
        builder.AppendLine($"- 源文件：`{Path.GetFileName(package.SourceFile)}`（{ExternalTool.Bytes(package.SourceBytes)}）");
        builder.AppendLine($"- Visual Studio 版本：{package.VsBuildVersion ?? "未知"}；包内资源：{string.Join(", ", package.Resources)}");
        builder.AppendLine($"- 采集窗口：{model.StartLocal ?? "?"} → {model.EndLocal ?? "?"}（本地时间），时长 {model.DurationSeconds:F2} s");
        var machine = new List<string>();
        if (package.Stats is not null && package.Stats.Processors > 0)
        {
            machine.Add($"{package.Stats.Processors} 逻辑核");
        }
        if (package.Stats is not null && package.Stats.CpuSpeedMhz > 0)
        {
            machine.Add($"{package.Stats.CpuSpeedMhz} MHz");
        }
        if (!string.IsNullOrEmpty(model.Machine.DiskModel))
        {
            machine.Add($"磁盘 {model.Machine.DiskModel}");
        }
        if (!string.IsNullOrEmpty(model.Machine.OsProductName))
        {
            machine.Add(model.Machine.OsProductName!);
        }
        builder.AppendLine($"- 机器：{string.Join("，", machine)}");
        builder.AppendLine($"- 采样：ETW 采样剖析，间隔 {model.Machine.SampleIntervalMs:0.###} ms（1 个采样 ≈ 1 ms 单核 CPU 时间），系统级采样 {model.TotalSamples}，丢事件 {package.Stats?.LostEvents ?? 0}");
        builder.AppendLine($"- 目标进程：{model.TargetProcessName} (pid {model.TargetPid})，由诊断包的「用户代码模块」列表自动识别");
        if (!string.IsNullOrEmpty(model.Machine.BuildString))
        {
            builder.AppendLine($"- 系统版本：{model.Machine.BuildString}");
        }
        if (model.ProcessCommandLines.TryGetValue(model.TargetPid, out var commandLine))
        {
            builder.AppendLine($"- 命令行：`{commandLine}`");
        }
        var cpuText = package.ProcessCpu is null
            ? "（counters 文件缺失）"
            : $"平均 {package.ProcessCpu.Avg:F2}%，P95 {package.ProcessCpu.P95:F2}%，峰值 {package.ProcessCpu.Max:F2}%（相对整机）";
        builder.AppendLine($"- 进程 CPU（来自 VS counters）：{cpuText}");
        var coverage = model.Samples.Count == 0 ? 0 : (double)model.Stacks.Count / model.Samples.Count;
        builder.AppendLine($"- 栈覆盖：{model.Stacks.Count}/{model.Samples.Count} = {coverage:P0} 的采样带调用栈（自耗统计基于全部采样，包含耗时/调用链基于有栈采样）");
        builder.AppendLine($"- 符号解析：{stats.ResolvedSamples}/{model.TargetSamples} = {(double)stats.ResolvedSamples / total:P0} 的采样点解析到函数名");
        builder.AppendLine($"- 符号来源分布：{string.Join("，", stats.SymbolSources.OrderByDescending(p => p.Value).Select(p => $"{p.Key} {p.Value}"))}");
        builder.AppendLine();

        builder.AppendLine("### 1.1 模块与符号状态");
        builder.AppendLine();
        builder.AppendLine("| 模块 | 自耗采样 | 基址 | 大小 | 镜像时间戳 | PDB | 符号 |");
        builder.AppendLine("|---|---|---|---|---|---|---|");
        foreach (var module in model.Modules.Values.Where(m => m.Pid == model.TargetPid || m.Pid == 0).OrderByDescending(m => stats.SelfByModule.GetValueOrDefault(m.Name)))
        {
            var hasSymbols = symbols.HasModule(module.Name);
            var moduleSamples = stats.SelfByModule.GetValueOrDefault(module.Name);
            if (moduleSamples == 0 && !hasSymbols)
            {
                continue;
            }
            var pdb = module.PdbName is null ? "—" : $"{module.PdbName} ({module.PdbGuid}{module.PdbAge:x})";
            builder.AppendLine($"| {module.Name} | {moduleSamples} | 0x{module.Base:x} | 0x{module.Size:x} | {module.TimeDateStamp:x8} | {pdb} | {(hasSymbols ? "已解析" : "无（显示为 模块+偏移）")} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 2. 线程分布");
        builder.AppendLine();
        builder.AppendLine("| tid | 采样 | 占比 | 线程名 | 起始函数 |");
        builder.AppendLine("|---|---|---|---|---|");
        foreach (var thread in model.Threads.Values.Where(t => t.Pid == model.TargetPid && t.Samples > 0).OrderByDescending(t => t.Samples))
        {
            builder.AppendLine($"| {thread.Tid} | {thread.Samples} | {(double)thread.Samples / total:P1} | {thread.Name ?? "—"} | {thread.StartFunction ?? "—"} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 3. 组件占比（自耗）");
        builder.AppendLine();
        builder.AppendLine("| 组件 | 采样 | 占比 | 说明 |");
        builder.AppendLine("|---|---|---|---|");
        foreach (var component in stats.SelfByComponent.OrderByDescending(p => p.Value))
        {
            builder.AppendLine($"| {Describe(component.Key)} | {component.Value} | {(double)component.Value / total:P1} | {ComponentHint(component.Key)} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 4. 模块自耗");
        builder.AppendLine();
        builder.AppendLine("| 模块 | 采样 | 占比 |");
        builder.AppendLine("|---|---|---|");
        foreach (var module in stats.SelfByModule.OrderByDescending(p => p.Value).Take(options.Top))
        {
            builder.AppendLine($"| {module.Key} | {module.Value} | {(double)module.Value / total:P1} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 5. 源文件热点（自耗，按 文件:行 聚合）");
        builder.AppendLine();
        builder.AppendLine("| 文件:行 | 采样 | 占比 | 所在函数 | 所属模块 |");
        builder.AppendLine("|---|---|---|---|---|");
        foreach (var entry in stats.SelfByFileLine.OrderByDescending(p => p.Value.Samples).Take(options.Top))
        {
            builder.AppendLine($"| {entry.Key} | {entry.Value.Samples} | {(double)entry.Value.Samples / total:P1} | {Escape(Shorten(entry.Value.Function))} | {entry.Value.Module} |");
        }
        builder.AppendLine();
        builder.AppendLine("按文件汇总：");
        builder.AppendLine();
        foreach (var entry in stats.SelfByFile.OrderByDescending(p => p.Value.Samples).Take(Math.Min(options.Top, 30)))
        {
            builder.AppendLine($"- {entry.Key}: {entry.Value.Samples} 采样（{(double)entry.Value.Samples / total:P1}）");
        }
        builder.AppendLine();

        builder.AppendLine("## 6. Top 自耗函数（self time）");
        builder.AppendLine();
        builder.AppendLine("| 采样 | 占比 | 函数 | 位置 | 模块 |");
        builder.AppendLine("|---|---|---|---|---|");
        foreach (var entry in stats.SelfByFunction.OrderByDescending(p => p.Value.Samples).Take(options.Top))
        {
            builder.AppendLine($"| {entry.Value.Samples} | {(double)entry.Value.Samples / total:P1} | {Escape(entry.Key)} | {entry.Value.HottestLocation()} | {entry.Value.Module} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 7. Top 包含耗时函数（inclusive，调用链上出现过即计入）");
        builder.AppendLine();
        builder.AppendLine("| 采样 | 占比 | 函数 | 位置 | 模块 |");
        builder.AppendLine("|---|---|---|---|---|");
        foreach (var entry in stats.InclusiveByFunction.OrderByDescending(p => p.Value.Samples).Take(options.Top))
        {
            builder.AppendLine($"| {entry.Value.Samples} | {(double)entry.Value.Samples / total:P1} | {Escape(entry.Key)} | {entry.Value.HottestLocation()} | {entry.Value.Module} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 8. 最热调用链（叶子 → 调用者，最多 8 帧）");
        builder.AppendLine();
        builder.AppendLine("| 采样 | 占比 | 调用链 |");
        builder.AppendLine("|---|---|---|");
        foreach (var entry in stats.Chains.OrderByDescending(p => p.Value).Take(Math.Min(options.Top, 25)))
        {
            builder.AppendLine($"| {entry.Value} | {(double)entry.Value / total:P1} | {Escape(TrimChain(entry.Key))} |");
        }
        builder.AppendLine();

        builder.AppendLine("## 9. 按模块的包含耗时");
        builder.AppendLine();
        foreach (var entry in stats.InclusiveByModule.OrderByDescending(p => p.Value).Take(Math.Min(options.Top, 25)))
        {
            builder.AppendLine($"- {entry.Key}: {entry.Value}（{(double)entry.Value / total:P1}）");
        }
        builder.AppendLine();

        builder.AppendLine("## 10. 时间线（100 ms 桶的采样数，进程内时间 ms）");
        builder.AppendLine();
        builder.AppendLine("```");
        builder.AppendLine(string.Join(' ', stats.Timeline.OrderBy(p => p.Key).Select(p => $"{p.Key * 100}:{p.Value}")));
        builder.AppendLine("```");
        builder.AppendLine();

        builder.AppendLine("## 11. 口径与注意事项");
        builder.AppendLine();
        builder.AppendLine("- 采样单位：1 个采样 ≈ 1 ms 的单核 CPU 时间；占比 = 采样 / 目标进程采样总数。");
        builder.AppendLine("- 自耗（self）指该函数自身的采样；包含耗时（inclusive）指调用链上出现该函数的采样，会重复计入父函数，不能直接相加。");
        builder.AppendLine("- 未解析的采样点显示为 `模块+0x偏移`，它们仍计入占比，但无法归因到代码行。");
        builder.AppendLine("- 名称已做规范化：模板参数过长时折叠为 `…`，函数参数列表已去掉；同一函数的不同重载可能合并显示。");
        builder.AppendLine("- STL/工具链头文件（`xmemory`、`tuple`、`xhash`、`algorithm` 等）的热点是**模板实例化后的真实代码**，往往指向调用方的数据结构选择问题。");
        builder.AppendLine("- 本项目模块：" + (projectModules.Count == 0 ? "（无）" : string.Join(", ", projectModules.OrderBy(m => m))));
        builder.AppendLine();

        builder.AppendLine("## 12. 给分析者的约定（可跳过）");
        builder.AppendLine();
        builder.AppendLine("- 结论请引用「函数名 + 文件:行 + 采样占比」，并区分「自耗高」与「包含耗时高」。");
        builder.AppendLine("- 对每一条优化建议，请说明：预期收益（用采样占比估算）、改动位置、风险、以及如何验证（例如对比同一场景的 `report.md`）。");
        builder.AppendLine("- 若证据不足（例如热点落在未解析地址或第三方模块），请明确标注不确定，而不是猜测。");
        return builder.ToString();
    }

    private static string Shorten(string location) => location.Length == 0 ? "—" : location;

    private static string Escape(string text) => text.Replace("|", "\\|");

    private const int MaxChainLength = 280;

    /// <summary>Keeps chains readable: drop frames past the cap instead of emitting a 2 KB line.</summary>
    private static string TrimChain(string chain)
    {
        if (chain.Length <= MaxChainLength)
        {
            return chain;
        }
        var parts = chain.Split(" < ");
        var builder = new StringBuilder();
        foreach (var part in parts)
        {
            if (builder.Length > 0 && builder.Length + part.Length + 3 > MaxChainLength)
            {
                builder.Append(" < …");
                break;
            }
            if (builder.Length > 0)
            {
                builder.Append(" < ");
            }
            builder.Append(part.Length > 120 ? part[..120] + "…" : part);
        }
        return builder.ToString();
    }

    private static string Describe(string component) => component switch
    {
        "project-code" => "项目源码",
        "third-party-code" => "第三方源码",
        "stdlib-cxx" => "C++ 标准库/工具链",
        "windows-runtime" => "Windows 内核与运行时",
        "gpu-driver" => "GPU 驱动",
        "other-module" => "其他模块",
        _ => "未解析",
    };

    private static string ComponentHint(string component) => component switch
    {
        "project-code" => "最可操作的部分",
        "stdlib-cxx" => "通常反映容器/算法选择，而非库本身慢",
        "windows-runtime" => "锁、堆、系统调用；高占比往往指向争用或分配热点",
        "gpu-driver" => "驱动内部或等待 GPU；需结合 GPU 时间线判断",
        _ => "",
    };

    private static string WriteJson(Options options, PackageInfo package, TraceModel model, ReportStatistics stats)
    {
        var total = Math.Max(1, model.TargetSamples);
        object Rows(Dictionary<string, Aggregate> source) => source
            .OrderByDescending(p => p.Value.Samples)
            .Take(options.Top)
            .Select(p => new
            {
                name = p.Key,
                samples = p.Value.Samples,
                share = Math.Round((double)p.Value.Samples / total, 5),
                location = p.Value.HottestLocation(),
                module = p.Value.Module,
                threads = p.Value.Threads,
            })
            .ToArray();

        var payload = new
        {
            generator = "DiagSessionProfiler",
            generated_at = DateTime.Now.ToString("s", CultureInfo.InvariantCulture),
            source = new
            {
                file = Path.GetFileName(package.SourceFile),
                bytes = package.SourceBytes,
                visual_studio = package.VsBuildVersion,
                resources = package.Resources,
            },
            capture = new
            {
                start_local = model.StartLocal,
                end_local = model.EndLocal,
                duration_seconds = Math.Round(model.DurationSeconds, 3),
                sample_interval_ms = model.Machine.SampleIntervalMs,
                system_samples = model.TotalSamples,
                target_samples = model.TargetSamples,
                stack_coverage = model.Samples.Count == 0 ? 0 : Math.Round((double)model.Stacks.Count / model.Samples.Count, 4),
                lost_events = package.Stats?.LostEvents ?? 0,
                processors = package.Stats?.Processors ?? 0,
                cpu_speed_mhz = package.Stats?.CpuSpeedMhz ?? 0,
                disk = model.Machine.DiskModel,
                os = model.Machine.OsProductName,
                os_build = model.Machine.BuildString,
            },
            process = new
            {
                name = model.TargetProcessName,
                pid = model.TargetPid,
                command_line = model.ProcessCommandLines.GetValueOrDefault(model.TargetPid),
                cpu_percent = package.ProcessCpu is null ? null : new
                {
                    avg = Math.Round(package.ProcessCpu.Avg, 2),
                    p95 = Math.Round(package.ProcessCpu.P95, 2),
                    max = Math.Round(package.ProcessCpu.Max, 2),
                },
            },
            symbol_sources = stats.SymbolSources,
            components = stats.SelfByComponent.OrderByDescending(p => p.Value).ToDictionary(p => p.Key, p => new { samples = p.Value, share = Math.Round((double)p.Value / total, 5) }),
            modules = stats.SelfByModule.OrderByDescending(p => p.Value).Take(options.Top).ToDictionary(p => p.Key, p => new { samples = p.Value, share = Math.Round((double)p.Value / total, 5) }),
            threads = model.Threads.Values.Where(t => t.Pid == model.TargetPid && t.Samples > 0).OrderByDescending(t => t.Samples)
                .Select(t => new { tid = t.Tid, name = t.Name, start_function = t.StartFunction, samples = t.Samples, share = Math.Round((double)t.Samples / total, 5) }).ToArray(),
            self_by_function = Rows(stats.SelfByFunction),
            self_by_file_line = Rows(stats.SelfByFileLine),
            inclusive_by_function = Rows(stats.InclusiveByFunction),
            chains = stats.Chains.OrderByDescending(p => p.Value).Take(Math.Min(options.Top, 25))
                .Select(p => new { samples = p.Value, share = Math.Round((double)p.Value / total, 5), chain = p.Key }).ToArray(),
            timeline_100ms = stats.Timeline.OrderBy(p => p.Key).ToDictionary(p => (p.Key * 100).ToString(CultureInfo.InvariantCulture), p => p.Value),
        };
        return JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true });
    }

    private static string WritePrompt(TraceModel model)
    {
        var builder = new StringBuilder();
        builder.AppendLine("# 给大模型的提示词（配合 report.md 使用）");
        builder.AppendLine();
        builder.AppendLine("把 `report.md` 全文贴在下一条消息里，然后发送下面这段指令：");
        builder.AppendLine();
        builder.AppendLine("```text");
        builder.AppendLine($"你是一名 C++/图形引擎性能工程师。附件（report.md）是 AiluEngine 编辑器进程 {model.TargetProcessName} 的一次 ETW 采样剖析数据集，");
        builder.AppendLine($"包含采集环境、线程分布、模块/组件占比、源文件:行 热点、自耗与包含耗时 Top 表、最热调用链与时间线。");
        builder.AppendLine("请只依据报告中的数据作答，不要臆造函数或行号。输出结构：");
        builder.AppendLine();
        builder.AppendLine("1. 结论摘要（3-5 条，每条一句话 + 关键占比）");
        builder.AppendLine("2. 热点归因：把采样占比 ≥1% 的自耗热点分组（例如并行/同步、分配、容器查找、排序、渲染提交），");
        builder.AppendLine("   每组给出证据（函数名、文件:行、采样数与占比）与它们之间的因果链（用 report 里的调用链佐证）");
        builder.AppendLine("3. 优化建议：按「预期收益 × 实施成本」排序，每条包含：改动位置（文件:行）、具体做法、");
        builder.AppendLine("   预期能消除多少占比（用采样占比推算，并说明推算假设）、风险与回归点");
        builder.AppendLine("4. 需要补充的数据：如果某些占比高的部分无法归因（未解析地址、第三方模块），列出应该补采什么");
        builder.AppendLine("5. 验证方法：给出可复现的对比方式（同一场景重新采集后对比哪些表、期望看到什么变化）");
        builder.AppendLine();
        builder.AppendLine("对不确定的地方明确说明不确定，不要用「可能」「大概」掩盖证据缺口。");
        builder.AppendLine("```");
        return builder.ToString();
    }
}
