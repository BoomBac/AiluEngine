using System.Globalization;
using System.Text;
using System.Text.Json;

namespace Ailu.DiagSessionProfiler;

/// <summary>Stage 4: resolve symbols of the profiled application from the PDBs recorded in the trace.</summary>
public static class SymbolResolver
{
    private const int MaxUnresolvedDisplacement = 0x1000;

    public static void ResolveProjectSymbols(Options options, PackageInfo package, TraceModel model, SymbolTable table, Logger log)
    {
        var llvmPdbUtil = ToolLocator.LlvmTool(options, "llvm-pdbutil.exe", options.LlvmPdbUtil);
        var llvmSymbolizer = ToolLocator.LlvmTool(options, "llvm-symbolizer.exe", options.LlvmSymbolizer);
        if (llvmPdbUtil is null || llvmSymbolizer is null)
        {
            log.Warn("llvm-pdbutil/llvm-symbolizer not found (install the LLVM tools that ship with Visual Studio); project symbols will be skipped");
            return;
        }
        log.Info($"llvm tools: {llvmPdbUtil}");

        var addresses = CollectAddresses(model);
        var candidates = model.Modules.Values
            .Where(m => m.Pid == model.TargetPid && m.PdbGuid is not null && m.Name.Length > 0)
            .Where(m => addresses.ContainsKey(m.Name.ToLowerInvariant()))
            .OrderByDescending(m => addresses[m.Name.ToLowerInvariant()].Count)
            .ToList();

        if (candidates.Count == 0)
        {
            log.Warn("no project module carries a PDB identity in the trace");
            return;
        }
        log.Info($"{candidates.Count} module(s) with a recorded PDB identity in the profile");

        var symDir = Path.Combine(options.CacheFull, "sym");
        Directory.CreateDirectory(symDir);

        var count = 0;
        foreach (var module in candidates)
        {
            var key = $"{Path.GetFileNameWithoutExtension(module.Name)}_{module.PdbGuid}{module.PdbAge:x}";
            var cacheFile = Path.Combine(symDir, key + ".tsv");
            var offsets = addresses[module.Name.ToLowerInvariant()];
            if (!options.Force && File.Exists(cacheFile))
            {
                LoadCache(cacheFile, module.Name, table);
                log.Info($"  {module.Name}: reused {offsets.Count} addresses from cache");
                count++;
                continue;
            }

            var pdb = FindMatchingPdb(options, module, llvmPdbUtil, log);
            if (pdb is null)
            {
                continue;
            }
            var image = LocateImage(module, pdb, log);
            if (image is null)
            {
                log.Warn($"  {module.Name}: image file not found; cannot build a symbol context");
                continue;
            }

            var patchedDir = Path.Combine(symDir, "patched", key);
            Directory.CreateDirectory(patchedDir);
            var patchedImage = Path.Combine(patchedDir, Path.GetFileName(image));
            if (!PeImage.PatchCodeView(image, patchedImage, module.PdbGuid, module.PdbAge, module.PdbName ?? Path.GetFileNameWithoutExtension(image) + ".pdb"))
            {
                log.Warn($"  {module.Name}: no CodeView record to patch; skipping");
                continue;
            }
            var pdbLink = Path.Combine(patchedDir, Path.GetFileName(pdb));
            Link(pdb, pdbLink);

            var bytes = File.ReadAllBytes(patchedImage);
            var headers = PeImage.ReadHeaders(bytes);
            if (headers is null)
            {
                log.Warn($"  {module.Name}: patched image unreadable");
                continue;
            }
            var imageBase = PeImage.ReadImageBase(bytes, headers.Value.PeOffset, headers.Value.Magic);
            var sections = PeImage.ReadSections(bytes);
            if (sections.Count == 0)
            {
                continue;
            }

            var order = offsets.OrderBy(o => o).ToList();
            var input = string.Join('\n', order.Select(o => "0x" + (imageBase + (ulong)o).ToString("x")));
            var json = ExternalTool.RunWithInput(llvmSymbolizer, $"--obj={ExternalTool.Quote(patchedImage)} --output-style=JSON --functions=linkage --inlines", input, log);

            var resolved = new Dictionary<ulong, SymbolTable.Entry>();
            var index = 0;
            foreach (var line in json.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                if (!line.StartsWith('{'))
                {
                    continue;
                }
                if (index >= order.Count)
                {
                    break;
                }
                var offset = order[index++];
                var (function, file, lineNumber) = ParseSymbolizerJson(line);
                if (function is null && file is null)
                {
                    continue;
                }
                resolved[offset] = new SymbolTable.Entry
                {
                    Function = NameShortener.Canonicalize(function ?? ""),
                    File = file,
                    Line = lineNumber,
                    Source = "pdb",
                };
            }

            var fallback = BuildFunctionIndex(options, llvmPdbUtil, pdbLink, sections, patchedDir, key, log);
            var fallbackHits = 0;
            foreach (var offset in order)
            {
                if (resolved.ContainsKey(offset))
                {
                    continue;
                }
                if (fallback.TryGet(offset, out var name, out var displacement) && displacement <= MaxUnresolvedDisplacement)
                {
                    resolved[offset] = new SymbolTable.Entry
                    {
                        Function = NameShortener.Canonicalize(name),
                        Source = options.DeepSymbols ? "functions" : "publics",
                    };
                    fallbackHits++;
                }
            }

            var withFile = resolved.Values.Count(e => e.File is not null);
            var coverage = order.Count == 0 ? 0 : (double)resolved.Count / order.Count;
            log.Info($"  {module.Name}: {resolved.Count}/{order.Count} addresses symbolized ({withFile} with file:line, {fallbackHits} from the function index), coverage {coverage:P0}");
            if (coverage < 0.5 && order.Count > 50)
            {
                log.Warn($"  {module.Name}: low symbol coverage - the PDB may not match the profiled binary");
            }

            WriteCache(cacheFile, resolved);
            foreach (var pair in resolved)
            {
                table.Add(module.Name, pair.Key, pair.Value);
            }
            count++;
        }
        log.Info($"symbolized {count} module(s) from local PDBs; symbol table holds {table.Count} entries");
    }

    /// <summary>Unique offsets per module name (lower case) taken from samples and stack frames.</summary>
    private static Dictionary<string, HashSet<ulong>> CollectAddresses(TraceModel model)
    {
        var result = new Dictionary<string, HashSet<ulong>>(StringComparer.OrdinalIgnoreCase);
        void Add(int pid, long address)
        {
            var module = model.FindModule(pid, (ulong)address);
            if (module is null || module.Name.Length == 0)
            {
                return;
            }
            if (!result.TryGetValue(module.Name.ToLowerInvariant(), out var set))
            {
                set = [];
                result[module.Name.ToLowerInvariant()] = set;
            }
            set.Add((ulong)address - module.Base);
        }

        foreach (var sample in model.Samples)
        {
            Add(sample.Pid, sample.Address);
        }
        foreach (var stack in model.Stacks)
        {
            foreach (var frame in stack.Frames)
            {
                Add(stack.Pid, frame);
            }
        }
        return result;
    }

    /// <summary>
    /// Walks every plausible PDB location and returns the first one whose GUID+age matches the trace.
    /// A plain "file exists" check is not enough: after a rebuild the recorded path holds a PDB for the
    /// new binaries and symbolizing with it would silently attribute samples to the wrong functions.
    /// </summary>
    private static string? FindMatchingPdb(Options options, ModuleRecord module, string llvmPdbUtil, Logger log)
    {
        var identity = $"{module.PdbGuid}{module.PdbAge:x}";
        var candidates = new List<string?>();
        if (module.PdbName is not null && module.PdbGuid is not null)
        {
            foreach (var cache in SymbolCacheRoots(options))
            {
                candidates.Add(Path.Combine(cache, module.PdbName, identity, module.PdbName));
                candidates.Add(Path.Combine(cache, module.PdbName, identity.ToUpperInvariant(), module.PdbName));
            }
        }
        if (module.PdbPath is not null)
        {
            candidates.Add(DevicePathResolver.Resolve(module.PdbPath));
            candidates.Add(File.Exists(module.PdbPath) ? module.PdbPath : null);
        }
        var image = DevicePathResolver.Resolve(module.ImagePath);
        if (image is not null && module.PdbName is not null)
        {
            candidates.Add(Path.Combine(Path.GetDirectoryName(image)!, module.PdbName));
        }
        if (module.PdbPath is not null && module.PdbName is not null)
        {
            var recorded = DevicePathResolver.Resolve(module.PdbPath);
            if (recorded is not null)
            {
                candidates.Add(Path.Combine(Path.GetDirectoryName(recorded)!, module.PdbName));
            }
        }

        var existing = 0;
        string? mismatch = null;
        foreach (var candidate in candidates)
        {
            if (string.IsNullOrWhiteSpace(candidate) || !File.Exists(candidate))
            {
                continue;
            }
            existing++;
            var (guid, age) = ReadPdbIdentity(llvmPdbUtil, candidate, log);
            if (guid is null)
            {
                continue;
            }
            if (string.Equals(guid, module.PdbGuid, StringComparison.OrdinalIgnoreCase) && age == module.PdbAge)
            {
                return candidate;
            }
            mismatch ??= $"{Path.GetFileName(candidate)} ({guid}{age:x})";
        }

        if (existing == 0)
        {
            log.Info($"  {module.Name}: no local PDB ({identity}); will try the symbol server");
        }
        else
        {
            log.Warn($"  {module.Name}: no PDB matches the profiled build (need {identity}, found {mismatch}); the module was rebuilt after profiling - symbols skipped");
        }
        return null;
    }

    private static (string? Guid, uint Age) ReadPdbIdentity(string llvmPdbUtil, string pdb, Logger log)
    {
        var summary = ExternalTool.Run(llvmPdbUtil, $"dump -summary {ExternalTool.Quote(pdb)}", log);
        string? guid = null;
        uint age = 0;
        foreach (var raw in summary.Split('\n'))
        {
            var line = raw.Trim();
            if (line.StartsWith("GUID:", StringComparison.Ordinal))
            {
                guid = line[5..].Trim().Trim('{', '}').Replace("-", "").ToLowerInvariant();
            }
            else if (line.StartsWith("Age:", StringComparison.Ordinal))
            {
                uint.TryParse(line[4..].Trim(), out age);
            }
        }
        return (guid, age);
    }

    private static IEnumerable<string> SymbolCacheRoots(Options options)
    {
        var roots = new List<string>();
        if (!string.IsNullOrWhiteSpace(options.SymbolPath))
        {
            foreach (var entry in options.SymbolPath!.Split(';', StringSplitOptions.RemoveEmptyEntries))
            {
                var trimmed = entry.Trim();
                if (trimmed.Contains('*'))
                {
                    var bits = trimmed.Split('*');
                    if (bits.Length >= 2 && bits[1].Length > 0)
                    {
                        roots.Add(bits[1]);
                    }
                }
                else if (Directory.Exists(trimmed))
                {
                    roots.Add(trimmed);
                }
            }
        }
        var symbolPath = Environment.GetEnvironmentVariable("_NT_SYMBOL_PATH") ?? "";
        foreach (var entry in symbolPath.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            var bits = entry.Split('*');
            var path = bits.Length >= 2 ? bits[1] : entry;
            if (path.Length > 0 && Directory.Exists(path))
            {
                roots.Add(path);
            }
        }
        roots.Add(Path.Combine(Path.GetTempPath(), "SymbolCache"));
        roots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Temp", "SymbolCache"));
        roots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Symbols"));
        return roots.Distinct();
    }

    private static string? LocateImage(ModuleRecord module, string pdb, Logger log)
    {
        var candidates = new List<string?>
        {
            DevicePathResolver.Resolve(module.ImagePath),
        };
        foreach (var directory in new[] { Path.GetDirectoryName(pdb), module.PdbPath is null ? null : Path.GetDirectoryName(module.PdbPath) })
        {
            if (string.IsNullOrEmpty(directory) || module.Name.Length == 0)
            {
                continue;
            }
            candidates.Add(Path.Combine(directory, module.Name));
            candidates.Add(Path.Combine(directory, Path.GetFileNameWithoutExtension(module.Name) + ".dll"));
            candidates.Add(Path.Combine(directory, Path.GetFileNameWithoutExtension(module.Name) + ".exe"));
        }
        foreach (var candidate in candidates)
        {
            if (!string.IsNullOrWhiteSpace(candidate) && File.Exists(candidate))
            {
                return candidate;
            }
        }
        log.Warn($"  {module.Name}: neither the traced image path nor a sibling of the PDB exists on this machine");
        return null;
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateHardLinkW(string fileName, string existingFileName, IntPtr securityAttributes);

    private static void Link(string source, string target)
    {
        if (File.Exists(target))
        {
            return;
        }
        if (!CreateHardLinkW(target, source, IntPtr.Zero))
        {
            File.Copy(source, target, true);
        }
    }

    private static (string? Function, string? File, int Line) ParseSymbolizerJson(string line)
    {
        try
        {
            using var document = JsonDocument.Parse(line);
            if (!document.RootElement.TryGetProperty("Symbol", out var symbols) || symbols.GetArrayLength() == 0)
            {
                return (null, null, 0);
            }
            var first = symbols[0];
            var function = first.TryGetProperty("FunctionName", out var f) ? f.GetString() : null;
            var file = first.TryGetProperty("FileName", out var fl) ? fl.GetString() : null;
            var lineNumber = first.TryGetProperty("Line", out var l) ? l.GetInt32() : 0;
            if (string.IsNullOrEmpty(function) && string.IsNullOrEmpty(file))
            {
                // inline frames may still carry the outer function
                for (var i = 1; i < symbols.GetArrayLength(); i++)
                {
                    var frame = symbols[i];
                    var name = frame.TryGetProperty("FunctionName", out var n) ? n.GetString() : null;
                    if (!string.IsNullOrEmpty(name))
                    {
                        return (name, null, 0);
                    }
                }
            }
            return (string.IsNullOrEmpty(function) ? null : function, string.IsNullOrEmpty(file) ? null : file, lineNumber);
        }
        catch
        {
            return (null, null, 0);
        }
    }

    private sealed class FunctionIndex
    {
        private readonly List<(uint Rva, uint Size, string Name)> _entries = [];

        public void Add(uint rva, uint size, string name) => _entries.Add((rva, size, name));

        public void Sort() => _entries.Sort((a, b) => a.Rva.CompareTo(b.Rva));

        public int Count => _entries.Count;

        public bool TryGet(ulong offset, out string name, out long displacement)
        {
            name = "";
            displacement = 0;
            var low = 0;
            var high = _entries.Count - 1;
            var best = -1;
            while (low <= high)
            {
                var mid = (low + high) / 2;
                if (_entries[mid].Rva <= offset)
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
                return false;
            }
            var entry = _entries[best];
            displacement = (long)offset - entry.Rva;
            if (entry.Size > 0 && displacement >= entry.Size)
            {
                return false;
            }
            name = entry.Name;
            return true;
        }
    }

    /// <summary>Builds a compact rva to name index. Fast mode uses the public stream, deep mode uses module symbols.</summary>
    private static FunctionIndex BuildFunctionIndex(Options options, string llvmPdbUtil, string pdb, List<PeImage.Section> sections, string patchedDir, string key, Logger log)
    {
        var index = new FunctionIndex();
        var dumpFile = Path.Combine(patchedDir, key + (options.DeepSymbols ? ".symbols.txt" : ".publics.txt"));
        if (!File.Exists(dumpFile) || options.Force)
        {
            var mode = options.DeepSymbols ? "--symbols" : "--publics";
            ExternalTool.RunToFile(llvmPdbUtil, $"dump {mode} {ExternalTool.Quote(pdb)}", dumpFile, log);
        }

        var textVa = sections[0].VirtualAddress;
        string? pendingName = null;
        var pendingKind = 0; // 1 = public function, 2 = procedure with size
        uint pendingSize = 0;
        foreach (var raw in File.ReadLines(dumpFile))
        {
            var line = raw;
            if (pendingName is not null)
            {
                var addrIndex = line.IndexOf("addr = ", StringComparison.Ordinal);
                if (addrIndex >= 0)
                {
                    var flagsOk = pendingKind == 2 || line.Contains("flags = function", StringComparison.Ordinal);
                    var value = line[(addrIndex + 7)..];
                    var colon = value.IndexOf(':');
                    var comma = value.IndexOf(',');
                    if (flagsOk && colon > 0)
                    {
                        var sectionText = value[..colon].Trim();
                        var offsetText = comma > colon ? value[(colon + 1)..comma].Trim() : value[(colon + 1)..].Trim();
                        if (int.TryParse(sectionText, out var section) && section == 1
                            && uint.TryParse(offsetText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var offset))
                        {
                            index.Add(textVa + offset, pendingSize, pendingName);
                        }
                    }
                    pendingName = null;
                    pendingKind = 0;
                    pendingSize = 0;
                }
                continue;
            }

            var isPublic = line.Contains("S_PUB32", StringComparison.Ordinal);
            var isProcedure = line.Contains("S_GPROC32", StringComparison.Ordinal) || line.Contains("S_LPROC32", StringComparison.Ordinal);
            if (!isPublic && !isProcedure)
            {
                continue;
            }
            if (isPublic && options.DeepSymbols)
            {
                // deep mode is driven by procedure records; skip the public stream entirely
                continue;
            }
            var start = line.IndexOf("] `", StringComparison.Ordinal);
            if (start < 0)
            {
                continue;
            }
            start += 3;
            var end = line.LastIndexOf('`');
            if (end <= start)
            {
                continue;
            }
            pendingName = line[start..end];
            pendingKind = isProcedure ? 2 : 1;
            if (isProcedure)
            {
                var sizeIndex = line.IndexOf("code size = ", StringComparison.Ordinal);
                pendingSize = 0;
                if (sizeIndex > 0)
                {
                    var rest = line[(sizeIndex + 12)..].Trim();
                    var space = rest.IndexOf(' ');
                    if (space > 0)
                    {
                        rest = rest[..space];
                    }
                    uint.TryParse(rest, out pendingSize);
                }
            }
        }
        index.Sort();
        log.Info($"    function index: {index.Count} entries ({(options.DeepSymbols ? "module symbols" : "publics")})");
        return index;
    }

    private static void WriteCache(string path, Dictionary<ulong, SymbolTable.Entry> resolved)
    {
        using var writer = new StreamWriter(path, false, new UTF8Encoding(false));
        foreach (var pair in resolved.OrderBy(p => p.Key))
        {
            writer.WriteLine($"{pair.Key:x}\t{pair.Value.Function}\t{pair.Value.File}\t{pair.Value.Line}\t{pair.Value.Source}");
        }
    }

    private static void LoadCache(string path, string module, SymbolTable table)
    {
        foreach (var line in File.ReadLines(path))
        {
            var parts = line.Split('\t');
            if (parts.Length < 5)
            {
                continue;
            }
            table.Add(module, Convert.ToUInt64(parts[0], 16), new SymbolTable.Entry
            {
                Function = parts[1],
                File = parts[2].Length == 0 ? null : parts[2],
                Line = int.Parse(parts[3]),
                Source = parts[4],
            });
        }
    }
}
