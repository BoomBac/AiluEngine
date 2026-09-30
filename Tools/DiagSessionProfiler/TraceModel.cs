using System.Text;
using System.Text.Json;
using System.Xml.Linq;

namespace Ailu.DiagSessionProfiler;

/// <summary>Everything read from the .diagsession container itself.</summary>
public sealed class PackageInfo
{
    public string SourceFile = "";
    public long SourceBytes;
    public string? EtlPath;
    public string? CountersPath;
    public string? VsBuildVersion;
    public DateTime? PackageTimeUtc;
    public readonly List<string> ToolGuids = [];
    public readonly List<string> UserCodeModules = [];
    public readonly List<string> Resources = [];
    public CpuSeries? ProcessCpu;
    public TraceStats? Stats;

    public static PackageInfo Load(Options options, Logger log)
    {
        var info = new PackageInfo
        {
            SourceFile = options.InputFull,
            SourceBytes = new FileInfo(options.InputFull).Length,
        };

        var packageDir = Path.Combine(options.CacheFull, "package");
        if (options.Force && Directory.Exists(packageDir))
        {
            Directory.Delete(packageDir, true);
        }
        if (!Directory.Exists(packageDir) || Directory.GetFiles(packageDir, "*", SearchOption.AllDirectories).Length == 0)
        {
            Directory.CreateDirectory(packageDir);
            log.Info($"unpacking {Path.GetFileName(info.SourceFile)} ({ExternalTool.Bytes(info.SourceBytes)})");
            System.IO.Compression.ZipFile.ExtractToDirectory(info.SourceFile, packageDir);
        }
        else
        {
            log.Info("reusing unpacked package");
        }

        var metadataPath = Path.Combine(packageDir, "metadata.xml");
        if (File.Exists(metadataPath))
        {
            ParseMetadata(metadataPath, info);
        }
        else
        {
            log.Warn("metadata.xml missing in package");
        }

        info.EtlPath = Directory.GetFiles(packageDir, "*.etl", SearchOption.AllDirectories)
            .OrderByDescending(f => new FileInfo(f).Length)
            .FirstOrDefault();
        info.CountersPath = Directory.GetFiles(packageDir, "*.counters", SearchOption.AllDirectories).FirstOrDefault();

        if (info.CountersPath is not null)
        {
            info.ProcessCpu = ReadCounters(info.CountersPath, log);
        }

        if (info.EtlPath is null)
        {
            throw new InvalidOperationException("the package contains no .etl trace");
        }
        log.Info($"trace: {Path.GetFileName(info.EtlPath)} ({ExternalTool.Bytes(new FileInfo(info.EtlPath).Length)})");

        info.Stats = ReadTraceStats(options, info.EtlPath, log);
        return info;
    }

    private static void ParseMetadata(string path, PackageInfo info)
    {
        var doc = XDocument.Load(path);
        XNamespace ns = doc.Root?.Name.Namespace ?? XNamespace.None;

        foreach (var tool in doc.Descendants(ns + "Tool"))
        {
            var id = tool.Attribute("Id")?.Value;
            if (id is not null)
            {
                info.ToolGuids.Add(id);
            }
        }
        foreach (var resource in doc.Descendants(ns + "Resource"))
        {
            info.Resources.Add($"{resource.Attribute("Type")?.Value}: {resource.Attribute("Name")?.Value}");
        }
        foreach (var item in doc.Descendants(ns + "Item"))
        {
            var key = item.Attribute("Key")?.Value;
            var value = item.Value;
            if (key == "_BuildVersion")
            {
                info.VsBuildVersion = value;
            }
            else if (key == "UserCodeModules")
            {
                try
                {
                    using var json = JsonDocument.Parse(value);
                    foreach (var element in json.RootElement.EnumerateArray())
                    {
                        var text = element.GetString();
                        if (!string.IsNullOrWhiteSpace(text))
                        {
                            info.UserCodeModules.Add(text.Trim());
                        }
                    }
                }
                catch
                {
                    // not fatal: the list is only used to auto detect the profiled process
                }
            }
        }

        var added = doc.Descendants(ns + "Resource").Select(r => r.Attribute("TimeAddedUTC")?.Value).FirstOrDefault(v => v is not null);
        if (added is not null && long.TryParse(added, out var fileTime))
        {
            try
            {
                info.PackageTimeUtc = DateTime.FromFileTimeUtc(fileTime);
            }
            catch
            {
                // ignore out of range timestamps
            }
        }
    }

    /// <summary>Reads the VS counters file (UTF-16 JSON) and keeps the process CPU curve.</summary>
    private static CpuSeries? ReadCounters(string path, Logger log)
    {
        try
        {
            var text = File.ReadAllText(path, Encoding.Unicode);
            using var json = JsonDocument.Parse(text);
            var root = json.RootElement;
            var frequency = 10_000_000.0;
            if (root.TryGetProperty("qpcFrequency", out var freq))
            {
                frequency = ReadInt64(freq);
            }
            if (!root.TryGetProperty("counters", out var counters))
            {
                return null;
            }
            foreach (var counter in counters.EnumerateArray())
            {
                var id = counter.TryGetProperty("id", out var idElement) ? idElement.GetString() : null;
                if (id is null || !id.Contains("Process.CPU", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                var series = new CpuSeries { Frequency = frequency };
                if (counter.TryGetProperty("p", out var points))
                {
                    foreach (var point in points.EnumerateArray())
                    {
                        var t = ReadInt64(point.GetProperty("t")) / frequency;
                        var v = point.GetProperty("v").GetDouble();
                        series.Points.Add((t, v));
                    }
                }
                series.Summarize();
                log.Info($"process CPU from counters: avg {series.Avg:F2}%, p95 {series.P95:F2}%, max {series.Max:F2}% ({series.Points.Count} samples)");
                return series;
            }
        }
        catch (Exception ex)
        {
            log.Warn("cannot read counters file: " + ex.Message);
        }
        return null;
    }

    private static long ReadInt64(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Number)
        {
            return element.GetInt64();
        }
        var high = element.GetProperty("h").GetInt64();
        var low = element.GetProperty("l").GetInt64();
        return (long)(((ulong)high << 32) | (uint)low);
    }

    private static TraceStats? ReadTraceStats(Options options, string etlPath, Logger log)
    {
        var xperf = ToolLocator.Xperf(options);
        if (xperf is null)
        {
            log.Warn("xperf.exe not found; machine details will be limited");
            return null;
        }
        var statsFile = Path.Combine(Path.GetTempPath(), "diagsession_tracestats_" + Guid.NewGuid().ToString("N") + ".txt");
        string output;
        try
        {
            ExternalTool.Run(xperf, $"-i {ExternalTool.Quote(etlPath)} -o {ExternalTool.Quote(statsFile)} -a tracestats", log);
            output = File.Exists(statsFile) ? File.ReadAllText(statsFile) : "";
        }
        finally
        {
            try { File.Delete(statsFile); } catch { /* ignore */ }
        }
        var stats = new TraceStats();
        foreach (var raw in output.Split('\n'))
        {
            var line = raw.Trim();
            var parts = line.Split(':', 2);
            if (parts.Length != 2)
            {
                continue;
            }
            var key = parts[0].Trim();
            var value = parts[1].Trim();
            switch (key)
            {
                case "Number of Processors": stats.Processors = ParseInt(value); break;
                case "CPU Speed": stats.CpuSpeedMhz = ParseInt(value); break;
                case "OS Build Number": stats.OsBuild = value; break;
                case "OS Version": stats.OsVersion = value; break;
                case "Total # Lost Events": stats.LostEvents = ParseInt(value); break;
                case "Total # Lost Buffers": stats.LostBuffers = ParseInt(value); break;
                case "Start time (Local)": stats.StartLocal = value; break;
                case "End time (Local)": stats.EndLocal = value; break;
            }
        }
        var duration = output.Split('\n').FirstOrDefault(l => l.Contains("(+") && l.Contains(':'));
        if (duration is not null)
        {
            var start = duration.IndexOf("(+", StringComparison.Ordinal);
            var end = duration.IndexOf(')', start + 2);
            if (end > start)
            {
                var span = duration[(start + 2)..end].Trim();
                var bits = span.Split(':').Select(b => b.Trim()).ToArray();
                if (bits.Length == 4 && bits.All(b => double.TryParse(b, out _)))
                {
                    stats.DurationSeconds = double.Parse(bits[0]) * 86400 + double.Parse(bits[1]) * 3600 + double.Parse(bits[2]) * 60 + double.Parse(bits[3].Replace(',', '.'));
                }
            }
        }
        return stats;
    }

    private static int ParseInt(string value)
    {
        var digits = new string(value.TakeWhile(c => char.IsDigit(c) || c == '-').ToArray());
        return int.TryParse(digits, out var result) ? result : 0;
    }
}

public sealed class CpuSeries
{
    public double Frequency = 10_000_000;
    public readonly List<(double Time, double Value)> Points = [];
    public double Avg;
    public double P95;
    public double Max;
    public double DurationSeconds;

    public void Summarize()
    {
        if (Points.Count == 0)
        {
            return;
        }
        Avg = Points.Average(p => p.Value);
        Max = Points.Max(p => p.Value);
        var sorted = Points.Select(p => p.Value).OrderBy(v => v).ToArray();
        P95 = sorted[(int)Math.Min(sorted.Length - 1, Math.Floor(sorted.Length * 0.95))];
        DurationSeconds = Points.Max(p => p.Time);
    }
}

public sealed class TraceStats
{
    public int Processors;
    public int CpuSpeedMhz;
    public string? OsVersion;
    public string? OsBuild;
    public string? StartLocal;
    public string? EndLocal;
    public double DurationSeconds;
    public long LostEvents;
    public long LostBuffers;
}

/// <summary>Machine context scraped from the trace itself.</summary>
public sealed class MachineInfo
{
    public string? BuildString;
    public string? OsProductName;
    public string? DiskModel;
    public string? WinSprScores;
    public double SampleIntervalMs = 1.0;
    public int PointerSize = 8;
}

public sealed class ModuleRecord
{
    public int Pid;
    public string Name = "";
    public ulong Base;
    public ulong Size;
    public uint TimeDateStamp;
    public string? ImagePath;
    public string? PdbName;
    public string? PdbGuid;
    public uint PdbAge;
    public string? PdbPath;

    public string Identity => PdbGuid is null ? Name : $"{Name}!{PdbGuid}{PdbAge:x}";
}

public sealed class ThreadRecord
{
    public int Tid;
    public int Pid;
    public string? Name;
    public string? StartFunction;
    public long Samples;
}

public sealed class SampleRecord
{
    public double Time;
    public int Pid;
    public int Tid;
    public int Cpu;
    public long Address;
    public string Module = "";
    public string? StartFunction;
}

public sealed class StackRecord
{
    public int Pid;
    public int Tid;
    public long Leaf;
    public long[] Frames = [];
}

/// <summary>Parsed trace: everything the report needs, cached as TSV between runs.</summary>
public sealed class TraceModel
{
    public string TargetProcessName = "";
    public int TargetPid;
    public double DurationSeconds;
    public string? StartLocal;
    public string? EndLocal;
    public long TotalSamples;
    public readonly MachineInfo Machine = new();
    public readonly Dictionary<int, string> ProcessNames = [];
    public readonly Dictionary<int, string> ProcessCommandLines = [];
    public readonly Dictionary<int, long> ProcessSamples = [];
    public readonly Dictionary<(int Pid, ulong Base), ModuleRecord> Modules = [];
    public readonly Dictionary<int, ThreadRecord> Threads = [];
    public readonly List<SampleRecord> Samples = [];
    public readonly List<StackRecord> Stacks = [];
    public long TargetSamples;

    private readonly Dictionary<int, ModuleRecord[]> _sortedModules = [];

    public ModuleRecord GetOrCreateModule(int pid, ulong imageBase)
    {
        if (Modules.TryGetValue((pid, imageBase), out var existing))
        {
            return existing;
        }
        var created = new ModuleRecord { Pid = pid, Base = imageBase, Name = "" };
        Modules[(pid, imageBase)] = created;
        _sortedModules.Remove(pid);
        return created;
    }

    /// <summary>Finds the module containing an address: target process first, then pid 0 (kernel), then any process.</summary>
    public ModuleRecord? FindModule(int pid, ulong address)
        => LookupIn(pid, address) ?? LookupIn(0, address) ?? LookupAny(address);

    public ModuleRecord? FindModuleByName(string name)
    {
        foreach (var module in Modules.Values)
        {
            if (string.Equals(module.Name, name, StringComparison.OrdinalIgnoreCase))
            {
                return module;
            }
        }
        return null;
    }

    private ModuleRecord? LookupIn(int pid, ulong address)
    {
        if (!_sortedModules.TryGetValue(pid, out var list))
        {
            list = Modules.Values.Where(m => m.Pid == pid).OrderBy(m => m.Base).ToArray();
            _sortedModules[pid] = list;
        }
        var low = 0;
        var high = list.Length - 1;
        var best = -1;
        while (low <= high)
        {
            var mid = (low + high) / 2;
            if (list[mid].Base <= address)
            {
                best = mid;
                low = mid + 1;
            }
            else
            {
                high = mid - 1;
            }
        }
        if (best < 0)
        {
            return null;
        }
        var candidate = list[best];
        return candidate.Size == 0 || address < candidate.Base + candidate.Size ? candidate : null;
    }

    private ModuleRecord? LookupAny(ulong address)
    {
        foreach (var module in Modules.Values)
        {
            if (address >= module.Base && (module.Size == 0 || address < module.Base + module.Size))
            {
                return module;
            }
        }
        return null;
    }

    public void Save(string directory)
    {
        Directory.CreateDirectory(directory);
        using (var writer = new StreamWriter(Path.Combine(directory, "model.tsv"), false, new UTF8Encoding(false)))
        {
            writer.WriteLine($"target\t{TargetPid}\t{TargetProcessName}");
            writer.WriteLine($"duration\t{DurationSeconds:R}\t{StartLocal}\t{EndLocal}");
            writer.WriteLine($"samples\t{TotalSamples}\t{TargetSamples}");
            writer.WriteLine($"machine\t{Machine.SampleIntervalMs:R}\t{Machine.PointerSize}\t{Machine.BuildString}\t{Machine.OsProductName}\t{Machine.DiskModel}\t{Machine.WinSprScores}");
            foreach (var proc in ProcessNames)
            {
                writer.WriteLine($"process\t{proc.Key}\t{proc.Value}\t{ProcessSamples.GetValueOrDefault(proc.Key)}\t{ProcessCommandLines.GetValueOrDefault(proc.Key) ?? ""}");
            }
            foreach (var module in Modules.Values)
            {
                writer.WriteLine($"module\t{module.Pid}\t{module.Name}\t{module.Base:x}\t{module.Size:x}\t{module.TimeDateStamp:x8}\t{module.ImagePath}\t{module.PdbName}\t{module.PdbGuid}\t{module.PdbAge:x}\t{module.PdbPath}");
            }
            foreach (var thread in Threads.Values)
            {
                writer.WriteLine($"thread\t{thread.Tid}\t{thread.Pid}\t{thread.Name}\t{thread.StartFunction}\t{thread.Samples}");
            }
            foreach (var sample in Samples)
            {
                writer.WriteLine($"sample\t{sample.Time:R}\t{sample.Pid}\t{sample.Tid}\t{sample.Cpu}\t{sample.Address:x}\t{sample.Module}\t{sample.StartFunction}");
            }
            foreach (var stack in Stacks)
            {
                writer.WriteLine($"stack\t{stack.Pid}\t{stack.Tid}\t{stack.Leaf:x}\t{string.Join(' ', stack.Frames.Select(f => f.ToString("x")))}");
            }
        }
    }

    public static TraceModel Load(string directory)
    {
        var model = new TraceModel();
        foreach (var line in File.ReadLines(Path.Combine(directory, "model.tsv")))
        {
            var parts = line.Split('\t');
            switch (parts[0])
            {
                case "target":
                    model.TargetPid = int.Parse(parts[1]);
                    model.TargetProcessName = parts[2];
                    break;
                case "duration":
                    model.DurationSeconds = double.Parse(parts[1]);
                    model.StartLocal = NullIfEmpty(parts[2]);
                    model.EndLocal = NullIfEmpty(parts[3]);
                    break;
                case "samples":
                    model.TotalSamples = long.Parse(parts[1]);
                    model.TargetSamples = long.Parse(parts[2]);
                    break;
                case "machine":
                    model.Machine.SampleIntervalMs = double.Parse(parts[1]);
                    model.Machine.PointerSize = int.Parse(parts[2]);
                    model.Machine.BuildString = NullIfEmpty(parts[3]);
                    model.Machine.OsProductName = NullIfEmpty(parts[4]);
                    model.Machine.DiskModel = NullIfEmpty(parts[5]);
                    model.Machine.WinSprScores = NullIfEmpty(parts[6]);
                    break;
                case "process":
                    {
                        var pid = int.Parse(parts[1]);
                        model.ProcessNames[pid] = parts[2];
                        model.ProcessSamples[pid] = long.Parse(parts[3]);
                        if (parts.Length > 4 && parts[4].Length > 0)
                        {
                            model.ProcessCommandLines[pid] = parts[4];
                        }
                        break;
                    }
                case "module":
                    {
                        var module = new ModuleRecord
                        {
                            Pid = int.Parse(parts[1]),
                            Name = parts[2],
                            Base = Convert.ToUInt64(parts[3], 16),
                            Size = Convert.ToUInt64(parts[4], 16),
                            TimeDateStamp = Convert.ToUInt32(parts[5], 16),
                            ImagePath = NullIfEmpty(parts[6]),
                            PdbName = NullIfEmpty(parts[7]),
                            PdbGuid = NullIfEmpty(parts[8]),
                            PdbAge = parts[9].Length > 0 ? Convert.ToUInt32(parts[9], 16) : 0,
                            PdbPath = parts.Length > 10 ? NullIfEmpty(parts[10]) : null,
                        };
                        model.Modules[(module.Pid, module.Base)] = module;
                        break;
                    }
                case "thread":
                    {
                        var thread = new ThreadRecord
                        {
                            Tid = int.Parse(parts[1]),
                            Pid = int.Parse(parts[2]),
                            Name = NullIfEmpty(parts[3]),
                            StartFunction = NullIfEmpty(parts[4]),
                            Samples = parts.Length > 5 && parts[5].Length > 0 ? long.Parse(parts[5]) : 0,
                        };
                        model.Threads[thread.Tid] = thread;
                        break;
                    }
                case "sample":
                    model.Samples.Add(new SampleRecord
                    {
                        Time = double.Parse(parts[1]),
                        Pid = int.Parse(parts[2]),
                        Tid = int.Parse(parts[3]),
                        Cpu = int.Parse(parts[4]),
                        Address = Convert.ToInt64(parts[5], 16),
                        Module = parts[6],
                        StartFunction = NullIfEmpty(parts[7]),
                    });
                    break;
                case "stack":
                    {
                        var frames = parts[4].Length == 0
                            ? []
                            : parts[4].Split(' ').Select(f => (long)Convert.ToUInt64(f, 16)).ToArray();
                        model.Stacks.Add(new StackRecord
                        {
                            Pid = int.Parse(parts[1]),
                            Tid = int.Parse(parts[2]),
                            Leaf = (long)Convert.ToUInt64(parts[3], 16),
                            Frames = frames,
                        });
                        break;
                    }
            }
        }
        return model;
    }

    private static string? NullIfEmpty(string value) => string.IsNullOrEmpty(value) ? null : value;
}

/// <summary>Address to symbol map shared by all symbol sources.</summary>
public sealed class SymbolTable
{
    public sealed class Entry
    {
        public string Function = "";
        public string? File;
        public int Line;
        public string Source = "";
    }

    private readonly Dictionary<string, Entry> _map = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _moduleSource = new(StringComparer.Ordinal);

    private static string Key(string module, ulong offset) => module.ToLowerInvariant() + "|" + offset.ToString("x");

    public void Add(string module, ulong offset, Entry entry)
    {
        _map[Key(module, offset)] = entry;
        var key = module.ToLowerInvariant();
        if (!_moduleSource.TryGetValue(key, out var existing) || Rank(entry.Source) < Rank(existing))
        {
            _moduleSource[key] = entry.Source;
        }
    }

    private static int Rank(string source) => source switch
    {
        "pdb" => 0,
        "functions" => 1,
        "publics" => 2,
        _ => 3,
    };

    /// <summary>True when the module was symbolized from a locally available project PDB (not from a symbol server).</summary>
    public bool HasLocalSymbols(string module)
    {
        return _moduleSource.TryGetValue(module.ToLowerInvariant(), out var source) && source is "pdb" or "functions" or "publics";
    }

    public Entry? Lookup(string module, ulong offset) => _map.TryGetValue(Key(module, offset), out var entry) ? entry : null;

    public int Count => _map.Count;

    public bool HasModule(string module)
    {
        var prefix = module.ToLowerInvariant() + "|";
        return _map.Keys.Any(k => k.StartsWith(prefix, StringComparison.Ordinal));
    }

    public void Save(string path)
    {
        using var writer = new StreamWriter(path, false, new UTF8Encoding(false));
        foreach (var pair in _map)
        {
            writer.WriteLine($"{pair.Key}\t{pair.Value.Function}\t{pair.Value.File}\t{pair.Value.Line}\t{pair.Value.Source}");
        }
    }

    public void Load(string path)
    {
        if (!File.Exists(path))
        {
            return;
        }
        foreach (var line in File.ReadLines(path))
        {
            var parts = line.Split('\t');
            if (parts.Length < 5)
            {
                continue;
            }
            _map[parts[0]] = new Entry
            {
                Function = parts[1],
                File = parts[2].Length == 0 ? null : parts[2],
                Line = int.Parse(parts[3]),
                Source = parts[4],
            };
        }
    }
}
