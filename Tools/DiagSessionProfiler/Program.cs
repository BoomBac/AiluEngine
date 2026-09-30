using System.Diagnostics;
using System.Text;

namespace Ailu.DiagSessionProfiler;

/// <summary>Options parsed from the command line.</summary>
public sealed class Options
{
    public string Command = "all";
    public string Input = "";
    public string Output = "";
    public string Cache = "";
    public string? Process;
    public int Top = 40;
    public bool NoSystemSymbols;
    public bool DeepSymbols;
    public bool CommandLines;
    public bool Force;
    public bool KeepIntermediate;
    public string? Xperf;
    public string? LlvmPdbUtil;
    public string? LlvmSymbolizer;
    public string? DbgHelp;
    public string? SymbolPath;

    public string InputFull => Path.GetFullPath(Input);
    public string OutputFull => Path.GetFullPath(string.IsNullOrEmpty(Output)
        ? Path.Combine(Path.GetDirectoryName(InputFull) ?? ".", Path.GetFileNameWithoutExtension(InputFull) + ".profile")
        : Output);
    public string CacheFull => Path.GetFullPath(string.IsNullOrEmpty(Cache) ? Path.Combine(OutputFull, "cache") : Cache);
}

public static class Program
{
    public static int Main(string[] args)
    {
        var options = Parse(args, out var error);
        if (options is null)
        {
            Console.Error.WriteLine("DiagSessionProfiler: " + error);
            Console.Error.WriteLine();
            PrintUsage();
            return 2;
        }

        try
        {
            Directory.CreateDirectory(options.OutputFull);
            Directory.CreateDirectory(options.CacheFull);

            var log = new Logger(Path.Combine(options.OutputFull, "run.log"));

            PackageInfo? package = null;
            TraceModel? model = null;

            var doExtract = options.Command is "all" or "extract";
            var doSymbols = options.Command is "all" or "symbols";
            var doReport = options.Command is "all" or "report";

            if (doExtract || doSymbols || doReport)
            {
                package = PackageInfo.Load(options, log);
                model = TraceExtractor.Extract(options, package, log);
                if (options.CommandLines && !model.ProcessCommandLines.ContainsKey(model.TargetPid))
                {
                    TraceExtractor.ReadCommandLines(package.EtlPath!, model, log);
                    model.Save(Path.Combine(options.CacheFull, "model"));
                }
            }

            var symbols = new SymbolTable();
            if (doSymbols || doReport)
            {
                SymbolResolver.ResolveProjectSymbols(options, package!, model!, symbols, log);
                if (!options.NoSystemSymbols)
                {
                    SystemSymbolResolver.Resolve(options, model!, symbols, log);
                }
                symbols.Save(Path.Combine(options.CacheFull, "symbols.tsv"));
            }
            else if (File.Exists(Path.Combine(options.CacheFull, "symbols.tsv")))
            {
                symbols.Load(Path.Combine(options.CacheFull, "symbols.tsv"));
            }

            if (doReport)
            {
                ReportBuilder.Build(options, package!, model!, symbols, log);
            }

            log.Info("done.");
            log.Dispose();
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("DiagSessionProfiler: " + ex.Message);
            Console.Error.WriteLine(ex.StackTrace);
            return 1;
        }
    }

    private static Options? Parse(string[] args, out string error)
    {
        error = "";
        var options = new Options();
        if (args.Length == 0)
        {
            error = "no arguments";
            return null;
        }

        var first = args[0];
        if (first is "all" or "extract" or "symbols" or "report" or "usage" or "-h" or "--help")
        {
            options.Command = first is "usage" or "-h" or "--help" ? "help" : first;
            if (options.Command == "help")
            {
                error = "";
                return null;
            }
        }
        else if (!first.StartsWith('-'))
        {
            error = $"unknown command '{first}'";
            return null;
        }

        for (var i = 1; i < args.Length; i++)
        {
            var a = args[i];
            string Next()
            {
                if (i + 1 >= args.Length) throw new ArgumentException($"missing value for {a}");
                return args[++i];
            }

            switch (a)
            {
                case "-i" or "--input": options.Input = Next(); break;
                case "-o" or "--output": options.Output = Next(); break;
                case "-c" or "--cache": options.Cache = Next(); break;
                case "-p" or "--process": options.Process = Next(); break;
                case "--top": options.Top = int.Parse(Next()); break;
                case "--no-system-symbols": options.NoSystemSymbols = true; break;
                case "--deep-symbols": options.DeepSymbols = true; break;
                case "--command-lines": options.CommandLines = true; break;
                case "--force": options.Force = true; break;
                case "--keep-intermediate": options.KeepIntermediate = true; break;
                case "--xperf": options.Xperf = Next(); break;
                case "--llvm-pdbutil": options.LlvmPdbUtil = Next(); break;
                case "--llvm-symbolizer": options.LlvmSymbolizer = Next(); break;
                case "--dbghelp": options.DbgHelp = Next(); break;
                case "--symbol-path": options.SymbolPath = Next(); break;
                default:
                    error = $"unknown option '{a}'";
                    return null;
            }
        }

        if (string.IsNullOrEmpty(options.Input))
        {
            error = "--input is required";
            return null;
        }
        if (!File.Exists(options.InputFull))
        {
            error = $"input file not found: {options.InputFull}";
            return null;
        }
        return options;
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            DiagSessionProfiler - turn a Visual Studio .diagsession into LLM-ready profiling data.

            usage:
              DiagSessionProfiler all     -i <file.diagsession> [-o <dir>] [options]
              DiagSessionProfiler extract -i <file.diagsession> [-o <dir>] [options]
              DiagSessionProfiler symbols -i <file.diagsession> [-o <dir>] [options]
              DiagSessionProfiler report  -i <file.diagsession> [-o <dir>] [options]

            options:
              -i, --input <file>       input .diagsession (required)
              -o, --output <dir>       output directory (default: <input dir>/<name>.profile)
              -c, --cache <dir>        cache directory (default: <output>/cache)
              -p, --process <name>     profiled process name (default: auto detect)
                  --top <n>            rows per table in the report (default 40)
                  --no-system-symbols  skip Windows symbol download (msdl)
                  --deep-symbols       build function index from module symbols (slower, adds static functions)
                  --command-lines      also read process command lines (needs a full XML pass, slower)
                  --force              ignore cached stage outputs
                  --keep-intermediate  keep events.csv and other large intermediates
                  --xperf <path>              override xperf.exe
                  --llvm-pdbutil <path>       override llvm-pdbutil.exe
                  --llvm-symbolizer <path>    override llvm-symbolizer.exe
                  --dbghelp <path>            override dbghelp.dll (needs sibling symsrv.dll for msdl)
                  --symbol-path <path>        extra PDB search path (semicolon separated)

            outputs:
              report.md     self-describing profiling dataset for an LLM
              profile.json  same data, machine readable
              prompt.md     ready to paste prompt that asks a model for the analysis
              run.log       what the tool did
            """);
    }
}

/// <summary>Minimal timestamped logger writing to console and a file.</summary>
public sealed class Logger : IDisposable
{
    private readonly StreamWriter? _writer;
    private readonly Stopwatch _watch = Stopwatch.StartNew();

    public Logger(string? path)
    {
        if (string.IsNullOrEmpty(path))
        {
            return;
        }
        _writer = new StreamWriter(path, false, new UTF8Encoding(false)) { AutoFlush = true };
    }

    public void Info(string message)
    {
        var line = $"[{_watch.Elapsed.TotalSeconds,7:F1}s] {message}";
        Console.WriteLine(line);
        _writer?.WriteLine(line);
    }

    public void Warn(string message)
    {
        var line = $"[{_watch.Elapsed.TotalSeconds,7:F1}s] WARNING: {message}";
        Console.WriteLine(line);
        _writer?.WriteLine(line);
    }

    public void Dispose() => _writer?.Dispose();
}
