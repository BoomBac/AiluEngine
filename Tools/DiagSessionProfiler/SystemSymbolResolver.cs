using System.Runtime.InteropServices;
using System.Text;

namespace Ailu.DiagSessionProfiler;

/// <summary>Stage 5: resolve symbols for system modules through dbghelp + symsrv (msdl.microsoft.com).</summary>
public static class SystemSymbolResolver
{

    public static void Resolve(Options options, TraceModel model, SymbolTable table, Logger log)
    {
        var dbghelpPath = ToolLocator.DbgHelp(options);
        if (dbghelpPath is null)
        {
            log.Warn("dbghelp.dll not found; system symbols will be skipped");
            return;
        }

        var addresses = CollectAddresses(model);
        var pending = new List<(ModuleRecord Module, List<ulong> Offsets)>();
        foreach (var module in model.Modules.Values)
        {
            if ((module.Pid != model.TargetPid && module.Pid != 0) || module.Name.Length == 0 || module.PdbGuid is null)
            {
                continue;
            }
            if (!addresses.TryGetValue(module.Name.ToLowerInvariant(), out var offsets) || offsets.Count == 0)
            {
                continue;
            }
            if (table.Lookup(module.Name, offsets.First()) is not null)
            {
                continue; // already symbolized from a local PDB (the profiled project)
            }
            pending.Add((module, offsets.OrderBy(o => o).ToList()));
        }

        if (pending.Count == 0)
        {
            log.Info("no system modules left to symbolize");
            return;
        }

        var symDir = Path.Combine(options.CacheFull, "sym");
        Directory.CreateDirectory(symDir);

        var cacheRoot = Path.Combine(options.CacheFull, "mssym");
        Directory.CreateDirectory(cacheRoot);
        var hasSymsrv = ToolLocator.HasSymsrv(dbghelpPath);
        var searchPath = string.IsNullOrWhiteSpace(options.SymbolPath) ? null : options.SymbolPath;
        if (hasSymsrv)
        {
            var server = $"srv*{cacheRoot}*https://msdl.microsoft.com/download/symbols";
            searchPath = string.IsNullOrWhiteSpace(searchPath) ? server : searchPath + ";" + server;
        }
        else
        {
            log.Warn($"symsrv.dll is missing next to {dbghelpPath}; Windows symbols cannot be downloaded (only cached PDBs are used)");
            searchPath = string.IsNullOrWhiteSpace(searchPath) ? cacheRoot : searchPath + ";" + cacheRoot;
        }

        log.Info($"resolving {pending.Count} system module(s) through dbghelp{(hasSymsrv ? " + msdl" : "")}");
        using var dbghelp = new DbgHelp(dbghelpPath);
        dbghelp.Initialize(searchPath ?? cacheRoot);

        var resolvedModules = 0;
        var noImage = 0;
        var rebuilt = 0;
        var loadFailed = 0;
        var noSymbols = 0;
        foreach (var (module, offsets) in pending.OrderByDescending(p => p.Offsets.Count))
        {
            var key = $"{Path.GetFileNameWithoutExtension(module.Name)}_{module.PdbGuid}{module.PdbAge:x}.ms";
            var cacheFile = Path.Combine(symDir, key + ".tsv");
            if (!options.Force && File.Exists(cacheFile))
            {
                LoadCache(cacheFile, module.Name, table);
                resolvedModules++;
                continue;
            }

            var image = DevicePathResolver.Resolve(module.ImagePath);
            if (image is null)
            {
                noImage++;
                WriteCache(cacheFile, []);
                continue;
            }
            if (!TimestampMatches(image, module))
            {
                rebuilt++;
                WriteCache(cacheFile, []);
                continue;
            }
            var moduleWatch = System.Diagnostics.Stopwatch.StartNew();
            var loaded = dbghelp.LoadModule(image, module.Name, module.Base);
            if (loaded == 0)
            {
                loadFailed++;
                WriteCache(cacheFile, []);
                continue;
            }

            var entries = new Dictionary<ulong, SymbolTable.Entry>();
            foreach (var offset in offsets)
            {
                var (function, file, line) = dbghelp.Resolve(module.Base + offset);
                if (function is null)
                {
                    continue;
                }
                var demangled = function.StartsWith('?') ? dbghelp.Demangle(function) : function;
                entries[offset] = new SymbolTable.Entry
                {
                    Function = NameShortener.Canonicalize(demangled),
                    File = file,
                    Line = line,
                    Source = "msdl",
                };
            }
            if (entries.Count == 0)
            {
                noSymbols++;
                WriteCache(cacheFile, []);
                continue;
            }
            log.Info($"  {module.Name}: {entries.Count}/{offsets.Count} addresses symbolized ({moduleWatch.Elapsed.TotalSeconds:F1}s)");
            WriteCache(cacheFile, entries);
            foreach (var pair in entries)
            {
                table.Add(module.Name, pair.Key, pair.Value);
            }
            resolvedModules++;
        }
        log.Info($"system symbols resolved for {resolvedModules} module(s); skipped: {rebuilt} rebuilt after profiling, {noSymbols} without symbols on the server, {noImage} without an image file, {loadFailed} that dbghelp refused to load");
    }

    private static bool TimestampMatches(string image, ModuleRecord module)
    {
        try
        {
            var bytes = new byte[4096];
            using var stream = File.OpenRead(image);
            var read = stream.Read(bytes, 0, bytes.Length);
            if (read < 512)
            {
                return true;
            }
            var peOffset = BitConverter.ToInt32(bytes, 0x3C);
            if (peOffset <= 0 || peOffset + 16 > read)
            {
                return true;
            }
            var stamp = BitConverter.ToUInt32(bytes, peOffset + 8);
            return module.TimeDateStamp == 0 || stamp == module.TimeDateStamp || module.PdbGuid is null;
        }
        catch
        {
            return true;
        }
    }

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

    private static void WriteCache(string path, Dictionary<ulong, SymbolTable.Entry> entries)
    {
        using var writer = new StreamWriter(path, false, new UTF8Encoding(false));
        foreach (var pair in entries.OrderBy(p => p.Key))
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
