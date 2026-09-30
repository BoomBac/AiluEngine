using System.Globalization;
using System.Text;

namespace Ailu.DiagSessionProfiler;

/// <summary>Stages 1-3: dump the ETL with xperf and parse the event stream into a trace model.</summary>
public static class TraceExtractor
{
    private static readonly HashSet<string> SystemProcessBlocklist = new(StringComparer.OrdinalIgnoreCase)
    {
        "Idle", "System", "Secure System", "Registry", "MemCompression", "csrss", "smss", "wininit",
        "services", "lsass", "winlogon", "fontdrvhost", "dwm", "svchost", "explorer", "RuntimeBroker",
        "conhost", "dllhost", "SearchIndexer", "TextInputHost", "StartMenuExperienceHost",
    };

    public static TraceModel Extract(Options options, PackageInfo package, Logger log)
    {
        var modelDir = Path.Combine(options.CacheFull, "model");
        var modelFile = Path.Combine(modelDir, "model.tsv");
        if (!options.Force && File.Exists(modelFile))
        {
            log.Info("reusing cached trace model");
            var cached = TraceModel.Load(modelDir);
            LogTarget(cached, log);
            return cached;
        }

        var csv = Path.Combine(options.CacheFull, "events.csv");
        if (options.Force && File.Exists(csv))
        {
            File.Delete(csv);
        }
        if (!File.Exists(csv))
        {
            var xperf = ToolLocator.Xperf(options)
                ?? throw new InvalidOperationException("xperf.exe not found; install the Windows Performance Toolkit (Windows ADK)");
            log.Info("dumping events with xperf (this is the slow part of the pipeline)");
            var xperfOutput = ExternalTool.Run(xperf, $"-i {ExternalTool.Quote(package.EtlPath!)} -o {ExternalTool.Quote(csv)} -a dumper", log);
            if (!File.Exists(csv))
            {
                throw new InvalidOperationException("xperf did not produce events.csv: " + ExternalTool.Truncate(xperfOutput, 400));
            }
            log.Info($"events.csv written: {ExternalTool.Bytes(new FileInfo(csv).Length)}");
        }
        else
        {
            log.Info($"reusing events.csv ({ExternalTool.Bytes(new FileInfo(csv).Length)})");
        }

        var model = ParseCsv(csv, package, options.Process, log);
        model.Save(modelDir);
        log.Info($"trace model cached in {modelDir}");
        if (!options.KeepIntermediate)
        {
            try { File.Delete(csv); } catch { /* the CSV is only an intermediate */ }
        }
        LogTarget(model, log);
        return model;
    }

    private static void LogTarget(TraceModel model, Logger log)
        => log.Info($"target process: {model.TargetProcessName} (pid {model.TargetPid}) - {model.TargetSamples} of {model.TotalSamples} samples");

    private static TraceModel ParseCsv(string csvPath, PackageInfo package, string? requestedProcess, Logger log)
    {
        var model = new TraceModel();
        SampleRecord? pendingSample = null;
        var pendingFrames = new List<long>();
        var inData = false;
        var lineNumber = 0;
        var stackRows = 0;

        using (var reader = new StreamReader(csvPath))
        {
            string? line;
            while ((line = reader.ReadLine()) is not null)
            {
                lineNumber++;
                if (!inData)
                {
                    if (line.Contains("EndHeader", StringComparison.Ordinal))
                    {
                        inData = true;
                    }
                    continue;
                }
                var trimmed = line.TrimStart();
                if (trimmed.Length < 3)
                {
                    continue;
                }
                if (trimmed.StartsWith("OS Version:", StringComparison.Ordinal))
                {
                    ParseHeaderLine(trimmed, model);
                    continue;
                }

                var comma = trimmed.IndexOf(',');
                if (comma < 0)
                {
                    continue;
                }
                var type = trimmed[..comma].Trim();
                var fields = SplitCsv(trimmed);
                if (fields.Count < 3)
                {
                    continue;
                }

                if (type == "Stack")
                {
                    stackRows++;
                    if (fields.Count >= 5 && TryParseAddress(fields[4], out var frame))
                    {
                        pendingFrames.Add((long)frame);
                    }
                    continue;
                }

                if (pendingFrames.Count > 0)
                {
                    if (pendingSample is not null)
                    {
                        model.Stacks.Add(new StackRecord
                        {
                            Pid = pendingSample.Pid,
                            Tid = pendingSample.Tid,
                            Leaf = pendingSample.Address,
                            Frames = pendingFrames.ToArray(),
                        });
                    }
                    pendingFrames = [];
                }

                switch (type)
                {
                    case "SampledProfile" or "SampledProfileNmi":
                        {
                            if (fields.Count < 9)
                            {
                                break;
                            }
                            var (pid, _) = ParseProcess(fields[2]);
                            var leafField = fields[7];
                            var bang = leafField.IndexOf('!');
                            TryParseAddress(bang >= 0 ? leafField[(bang + 1)..] : leafField, out var address);
                            var sample = new SampleRecord
                            {
                                Time = double.Parse(fields[1], CultureInfo.InvariantCulture) / 1_000_000.0,
                                Pid = pid,
                                Tid = ParseInt(fields[3]),
                                Cpu = ParseInt(fields[5]),
                                Address = (long)address,
                                Module = ModuleOfImageField(fields[7]),
                                StartFunction = ModuleOfImageField(fields[6]),
                            };
                            model.TotalSamples++;
                            model.ProcessSamples[pid] = model.ProcessSamples.GetValueOrDefault(pid) + 1;
                            if (model.Threads.TryGetValue(sample.Tid, out var thread))
                            {
                                if (thread.Pid == 0)
                                {
                                    thread.Pid = pid;
                                }
                            }
                            else
                            {
                                model.Threads[sample.Tid] = new ThreadRecord { Tid = sample.Tid, Pid = pid, StartFunction = sample.StartFunction };
                            }
                            pendingSample = sample;
                            model.Samples.Add(sample);
                            break;
                        }
                    case "ImageId":
                        {
                            if (fields.Count < 7)
                            {
                                break;
                            }
                            var (pid, _) = ParseProcess(fields[2]);
                            if (!TryParseAddress(fields[3], out var imageBase) || !TryParseAddress(fields[4], out var imageSize))
                            {
                                break;
                            }
                            var module = model.GetOrCreateModule(pid, imageBase);
                            module.Size = imageSize;
                            module.TimeDateStamp = (uint)ParseHex(fields[5]);
                            var name = StripQuotes(fields[6]);
                            if (name.Length > 0)
                            {
                                module.Name = name;
                            }
                            break;
                        }
                    case "I-DCStart" or "I-Start":
                        {
                            if (fields.Count < 9)
                            {
                                break;
                            }
                            var (pid, _) = ParseProcess(fields[2]);
                            if (!TryParseAddress(fields[3], out var imageBase))
                            {
                                break;
                            }
                            var module = model.GetOrCreateModule(pid, imageBase);
                            if (TryParseAddress(fields[4], out var end) && end > imageBase)
                            {
                                module.Size = end - imageBase;
                            }
                            foreach (var candidate in fields.Skip(7))
                            {
                                var path = StripQuotes(candidate);
                                if (path.Contains('\\') && !path.StartsWith(@"\SystemRoot", StringComparison.OrdinalIgnoreCase) && !path.StartsWith(@"\??", StringComparison.Ordinal))
                                {
                                    module.ImagePath = path;
                                    if (module.Name.Length == 0)
                                    {
                                        module.Name = Path.GetFileName(path);
                                    }
                                    break;
                                }
                            }
                            break;
                        }
                    case "DbgId/RSDS" or "DbgId/ILRSDS":
                        {
                            if (fields.Count < 7)
                            {
                                break;
                            }
                            var (pid, _) = ParseProcess(fields[2]);
                            if (!TryParseAddress(fields[3], out var imageBase))
                            {
                                break;
                            }
                            var module = model.GetOrCreateModule(pid, imageBase);
                            module.PdbGuid = StripQuotes(fields[4]).Trim('{', '}').Replace("-", "").ToLowerInvariant();
                            module.PdbAge = (uint)ParseHex(fields[5]);
                            var value = StripQuotes(fields[6]);
                            if (value.Length == 0)
                            {
                                break;
                            }
                            if (type == "DbgId/ILRSDS" || value.Contains('\\'))
                            {
                                module.PdbPath = value;
                                module.PdbName = Path.GetFileName(value);
                            }
                            else
                            {
                                module.PdbName = value;
                            }
                            break;
                        }
                    case "P-DCStart" or "P-Start":
                        {
                            var (pid, name) = ParseProcess(fields[2]);
                            if (pid != 0 && name.Length > 0 && !name.StartsWith('<'))
                            {
                                model.ProcessNames[pid] = name;
                            }
                            break;
                        }
                    case "ThreadName":
                        {
                            if (fields.Count < 5)
                            {
                                break;
                            }
                            var (pid, _) = ParseProcess(fields[2]);
                            var tid = ParseInt(fields[3]);
                            if (!model.Threads.TryGetValue(tid, out var thread))
                            {
                                thread = new ThreadRecord { Tid = tid, Pid = pid };
                                model.Threads[tid] = thread;
                            }
                            thread.Name = StripQuotes(fields[4]);
                            break;
                        }
                    case "ProfileFreq-DCStart" or "ProfileFreq-SetInterval":
                        {
                            if (fields.Count >= 6 && long.TryParse(fields[5], out var interval) && interval > 0)
                            {
                                model.Machine.SampleIntervalMs = interval / 10_000.0;
                            }
                            break;
                        }
                    case "BuildInfo":
                        {
                            if (fields.Count >= 5)
                            {
                                model.Machine.BuildString = StripQuotes(fields[3]);
                                model.Machine.OsProductName = StripQuotes(fields[4]);
                            }
                            break;
                        }
                    case "PhysicalDisk":
                        {
                            var longest = fields.OrderByDescending(f => f.Length).FirstOrDefault();
                            if (longest is { Length: > 8 } && longest.Any(char.IsLetter))
                            {
                                model.Machine.DiskModel = longest;
                            }
                            break;
                        }
                    case "WinSAT_WinSPR":
                        {
                            if (fields.Count >= 3)
                            {
                                model.Machine.WinSprScores = StripQuotes(fields[2]);
                            }
                            break;
                        }
                }
            }
        }

        if (pendingFrames.Count > 0 && pendingSample is not null)
        {
            model.Stacks.Add(new StackRecord
            {
                Pid = pendingSample.Pid,
                Tid = pendingSample.Tid,
                Leaf = pendingSample.Address,
                Frames = pendingFrames.ToArray(),
            });
        }

        model.DurationSeconds = package.Stats?.DurationSeconds ?? 0;
        model.StartLocal = package.Stats?.StartLocal;
        model.EndLocal = package.Stats?.EndLocal;

        SelectTarget(model, package, requestedProcess, log);

        var targetPid = model.TargetPid;
        model.Samples.RemoveAll(s => s.Pid != targetPid);
        model.Stacks.RemoveAll(s => s.Pid != targetPid);
        model.TargetSamples = model.Samples.Count;

        var perThread = model.Samples.GroupBy(s => s.Tid).ToDictionary(g => g.Key, g => (long)g.Count());
        foreach (var thread in model.Threads.Values)
        {
            if (thread.Pid == targetPid)
            {
                thread.Samples = perThread.GetValueOrDefault(thread.Tid);
            }
        }

        log.Info($"parsed {lineNumber} CSV lines: {model.TotalSamples} samples, {stackRows} stack frame rows, {model.Stacks.Count} stacks for the target");
        return model;
    }

    private static void SelectTarget(TraceModel model, PackageInfo package, string? requestedProcess, Logger log)
    {
        if (!string.IsNullOrWhiteSpace(requestedProcess))
        {
            var wanted = Path.GetFileNameWithoutExtension(requestedProcess);
            var match = model.ProcessNames.FirstOrDefault(p => string.Equals(Path.GetFileNameWithoutExtension(p.Value), wanted, StringComparison.OrdinalIgnoreCase));
            if (match.Value is not null)
            {
                model.TargetPid = match.Key;
                model.TargetProcessName = match.Value;
                log.Info($"target process forced by --process: {match.Value} (pid {match.Key})");
                return;
            }
            log.Warn($"process '{requestedProcess}' not found in the trace; falling back to auto detection");
        }

        var moduleNames = package.UserCodeModules
            .Select(m => Path.GetFileNameWithoutExtension(m))
            .Where(m => m.Length > 2)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        var byModule = model.ProcessSamples
            .Where(p => p.Key != 0 && model.ProcessNames.ContainsKey(p.Key))
            .Where(p => moduleNames.Contains(Path.GetFileNameWithoutExtension(model.ProcessNames[p.Key])))
            .OrderByDescending(p => p.Value)
            .FirstOrDefault();

        if (byModule.Value > 0)
        {
            model.TargetPid = byModule.Key;
            model.TargetProcessName = model.ProcessNames[byModule.Key];
            log.Info($"target process auto detected from the diagnostics user-code module list: {model.TargetProcessName}");
            return;
        }

        var bySamples = model.ProcessSamples
            .Where(p => p.Key != 0 && model.ProcessNames.ContainsKey(p.Key))
            .Where(p => !SystemProcessBlocklist.Contains(Path.GetFileNameWithoutExtension(model.ProcessNames[p.Key])))
            .OrderByDescending(p => p.Value)
            .FirstOrDefault();

        if (bySamples.Value == 0)
        {
            bySamples = model.ProcessSamples.OrderByDescending(p => p.Value).First();
        }
        model.TargetPid = bySamples.Key;
        model.TargetProcessName = model.ProcessNames.GetValueOrDefault(bySamples.Key, $"pid {bySamples.Key}");
        log.Warn($"target process guessed from sample counts: {model.TargetProcessName} ({bySamples.Value} samples) - use --process to override");
    }

    private static void ParseHeaderLine(string line, TraceModel model)
    {
        foreach (var part in line.Split(','))
        {
            var bits = part.Split(':', 2);
            if (bits.Length == 2 && bits[0].Trim() == "PointerSize" && int.TryParse(bits[1].Trim(), out var pointerSize))
            {
                model.Machine.PointerSize = pointerSize;
            }
        }
    }

    /// <summary>Turns "Engine.dll!0x00007ffcd71d1abf" into "Engine.dll".</summary>
    private static string ModuleOfImageField(string field)
    {
        var bang = field.IndexOf('!');
        return bang < 0 ? field : field[..bang];
    }

    private static (int Pid, string Name) ParseProcess(string field)
    {
        var open = field.LastIndexOf('(');
        if (open < 0)
        {
            return (0, field.Trim());
        }
        var close = field.LastIndexOf(')');
        var pidText = close > open ? field[(open + 1)..close].Trim() : "";
        return (int.TryParse(pidText, out var pid) ? pid : 0, field[..open].Trim());
    }

    private static int ParseInt(string value) => int.TryParse(value.Trim(), out var result) ? result : 0;

    private static long ParseHex(string value)
    {
        var text = value.Trim().Trim('{', '}');
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            text = text[2..];
        }
        return long.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var result) ? result : 0;
    }

    private static bool TryParseAddress(string value, out ulong address)
    {
        var text = value.Trim().Trim('{', '}').Replace("`", "");
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            text = text[2..];
        }
        return ulong.TryParse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out address);
    }

    private static string StripQuotes(string value) => value.Trim().Trim('"');

    /// <summary>Splits one CSV row, honouring quoted fields.</summary>
    public static List<string> SplitCsv(string line)
    {
        var result = new List<string>(16);
        var builder = new StringBuilder();
        var quoted = false;
        for (var i = 0; i < line.Length; i++)
        {
            var c = line[i];
            if (quoted)
            {
                if (c == '"')
                {
                    if (i + 1 < line.Length && line[i + 1] == '"')
                    {
                        builder.Append('"');
                        i++;
                    }
                    else
                    {
                        quoted = false;
                    }
                }
                else
                {
                    builder.Append(c);
                }
            }
            else if (c == '"')
            {
                quoted = true;
            }
            else if (c == ',')
            {
                result.Add(builder.ToString().Trim());
                builder.Clear();
            }
            else
            {
                builder.Append(c);
            }
        }
        result.Add(builder.ToString().Trim());
        return result;
    }

    /// <summary>Reads process command lines out of a tracerpt XML dump (optional, slow).</summary>
    public static void ReadCommandLines(string etlPath, TraceModel model, Logger log)
    {
        var tracerpt = Path.Combine(Environment.SystemDirectory, "tracerpt.exe");
        if (!File.Exists(tracerpt))
        {
            log.Warn("tracerpt.exe not found; command lines unavailable");
            return;
        }
        var xml = Path.Combine(Path.GetTempPath(), "diagsession_processes_" + Guid.NewGuid().ToString("N") + ".xml");
        try
        {
            log.Info("dumping process events for command lines (tracerpt XML pass)");
            var tracerptOutput = ExternalTool.Run(tracerpt, $"{ExternalTool.Quote(etlPath)} -o {ExternalTool.Quote(xml)} -of XML -y", log);
            if (!File.Exists(xml))
            {
                log.Warn("tracerpt produced no output: " + ExternalTool.Truncate(tracerptOutput, 200));
                return;
            }
            log.Info($"process dump: {ExternalTool.Bytes(new FileInfo(xml).Length)}");
            StreamXmlProcesses(xml, model);
            log.Info($"command lines collected for {model.ProcessCommandLines.Count} processes");
        }
        catch (Exception ex)
        {
            log.Warn("command line extraction failed: " + ex.Message);
        }
        finally
        {
            try { File.Delete(xml); } catch { /* ignore */ }
        }
    }

    private static void StreamXmlProcesses(string xmlPath, TraceModel model)
    {
        var settings = new System.Xml.XmlReaderSettings
        {
            IgnoreWhitespace = true,
            IgnoreComments = true,
            DtdProcessing = System.Xml.DtdProcessing.Ignore,
        };
        using var reader = System.Xml.XmlReader.Create(xmlPath, settings);
        var data = new Dictionary<string, string>(StringComparer.Ordinal);
        string? dataName = null;
        var text = new StringBuilder();
        while (reader.Read())
        {
            switch (reader.NodeType)
            {
                case System.Xml.XmlNodeType.Element when reader.LocalName == "Event":
                    data.Clear();
                    break;
                case System.Xml.XmlNodeType.Element when reader.LocalName == "Data":
                    dataName = reader.GetAttribute("Name");
                    text.Clear();
                    break;
                case System.Xml.XmlNodeType.Text or System.Xml.XmlNodeType.CDATA when dataName is not null:
                    text.Append(reader.Value);
                    break;
                case System.Xml.XmlNodeType.EndElement when reader.LocalName == "Data" && dataName is not null:
                    data[dataName] = text.ToString().Trim();
                    dataName = null;
                    break;
                case System.Xml.XmlNodeType.EndElement when reader.LocalName == "Event":
                    if (data.TryGetValue("CommandLine", out var commandLine)
                        && data.TryGetValue("ProcessId", out var pidText)
                        && commandLine.Length > 0)
                    {
                        var pid = (int)ParseHex(pidText);
                        if (pid != 0)
                        {
                            model.ProcessCommandLines[pid] = commandLine;
                        }
                    }
                    break;
            }
        }
    }
}
