using System.Diagnostics;
using System.Text;

namespace Ailu.DiagSessionProfiler;

/// <summary>Locates the external tools the profiler depends on.</summary>
public static class ToolLocator
{
    public static string? Xperf(Options options)
    {
        var candidates = new List<string?>();
        candidates.Add(options.Xperf);
        candidates.Add(Environment.GetEnvironmentVariable("DIAGSESSION_XPERF"));
        var kits = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Windows Kits", "10", "Windows Performance Toolkit", "xperf.exe");
        candidates.Add(kits);
        foreach (var dir in SafeDirs(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Windows Kits")))
        {
            candidates.Add(Path.Combine(dir, "Windows Performance Toolkit", "xperf.exe"));
        }
        candidates.Add(FindOnPath("xperf.exe"));
        return FirstExisting(candidates);
    }

    public static string? LlvmTool(Options options, string toolName, string? explicitPath)
    {
        var candidates = new List<string?> { explicitPath };
        candidates.Add(Environment.GetEnvironmentVariable("DIAGSESSION_LLVM_BIN") is { Length: > 0 } env ? Path.Combine(env, toolName) : null);

        foreach (var visualStudioRoot in VisualStudioRoots())
        {
            var llvmDirs = SafeDirs(Path.Combine(visualStudioRoot, "VC", "Tools", "Llvm"))
                .OrderByDescending(dir => Path.GetFileName(dir).Equals("x64", StringComparison.OrdinalIgnoreCase))
                .ToArray();
            foreach (var dir in llvmDirs)
            {
                candidates.Add(Path.Combine(dir, "bin", toolName));
            }
        }
        candidates.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "LLVM", "bin", toolName));
        candidates.Add(FindOnPath(toolName));
        return FirstExisting(candidates);
    }

    public static string? DbgHelp(Options options)
    {
        var candidates = new List<string?>();
        candidates.Add(options.DbgHelp);
        candidates.Add(Environment.GetEnvironmentVariable("DIAGSESSION_DBGHELP"));
        var kits = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Windows Kits", "10", "Debuggers", "x64", "dbghelp.dll");
        candidates.Add(kits);
        foreach (var dir in SafeDirs(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Windows Kits")))
        {
            candidates.Add(Path.Combine(dir, "Debuggers", "x64", "dbghelp.dll"));
        }
        candidates.Add(Path.Combine(Environment.SystemDirectory, "dbghelp.dll"));
        return FirstExisting(candidates);
    }

    /// <summary>True when the dbghelp next to the given file also has symsrv (needed for symbol servers).</summary>
    public static bool HasSymsrv(string dbghelpPath)
    {
        var dir = Path.GetDirectoryName(dbghelpPath);
        return dir is not null && File.Exists(Path.Combine(dir, "symsrv.dll"));
    }

    public static IEnumerable<string> VisualStudioRoots()
    {
        var roots = new List<string>
        {
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Microsoft Visual Studio"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio"),
        };
        var vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (File.Exists(vswhere))
        {
            var output = ExternalTool.Run(vswhere, "-all -prerelease -property installationPath", null);
            foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                roots.Add(line);
            }
        }
        return roots;
    }

    private static IEnumerable<string> SafeDirs(string root)
    {
        if (!Directory.Exists(root))
        {
            return Array.Empty<string>();
        }
        try
        {
            return Directory.GetDirectories(root, "*", SearchOption.AllDirectories);
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    private static string? FindOnPath(string fileName)
    {
        var path = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var dir in path.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            try
            {
                var full = Path.Combine(dir.Trim(), fileName);
                if (File.Exists(full))
                {
                    return full;
                }
            }
            catch
            {
                // ignore malformed PATH entries
            }
        }
        return null;
    }

    /// <summary>Returns the first existing candidate that is a native x64 binary (ARM64 builds of the LLVM tools cannot be launched).</summary>
    private static string? FirstExisting(IEnumerable<string?> candidates)
    {
        string? fallback = null;
        foreach (var candidate in candidates)
        {
            if (string.IsNullOrWhiteSpace(candidate) || !File.Exists(candidate))
            {
                continue;
            }
            var machine = ReadMachineType(candidate);
            if (machine == 0x8664)
            {
                return candidate;
            }
            fallback ??= candidate;
        }
        return fallback;
    }

    /// <summary>Reads the PE machine field (0x8664 = x64, 0xAA64 = ARM64, 0x14C = x86); 0 when unknown.</summary>
    public static ushort ReadMachineType(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            var header = new byte[0x40];
            if (stream.Read(header, 0, header.Length) < header.Length || header[0] != (byte)'M' || header[1] != (byte)'Z')
            {
                return 0;
            }
            var peOffset = BitConverter.ToInt32(header, 0x3C);
            if (peOffset <= 0)
            {
                return 0;
            }
            stream.Position = peOffset;
            var pe = new byte[6];
            if (stream.Read(pe, 0, pe.Length) < pe.Length)
            {
                return 0;
            }
            return BitConverter.ToUInt16(pe, 4);
        }
        catch
        {
            return 0;
        }
    }
}

/// <summary>Runs external console tools.</summary>
public static class ExternalTool
{
    /// <summary>Runs a tool and captures stdout (use only for small outputs).</summary>
    public static string Run(string exe, string arguments, Logger? log)
    {
        var info = new ProcessStartInfo(exe, arguments)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        using var process = Process.Start(info) ?? throw new InvalidOperationException($"cannot start {exe}");
        var stdoutTask = process.StandardOutput.ReadToEndAsync();
        var stderrTask = process.StandardError.ReadToEndAsync();
        process.WaitForExit();
        var stdout = stdoutTask.GetAwaiter().GetResult();
        var stderr = stderrTask.GetAwaiter().GetResult();
        if (process.ExitCode != 0 && log is not null)
        {
            log.Warn($"{Path.GetFileName(exe)} exited with {process.ExitCode}: {Truncate(stderr.Length > 0 ? stderr : stdout, 400)}");
        }
        return stdout;
    }

    /// <summary>Runs a tool that prints its result to stdout and streams that straight into a file.</summary>
    public static bool RunToFile(string exe, string arguments, string outputFile, Logger log)
    {
        var info = new ProcessStartInfo(exe, arguments)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        using var process = Process.Start(info) ?? throw new InvalidOperationException($"cannot start {exe}");
        var stderrTask = process.StandardError.ReadToEndAsync();
        using (var file = File.Create(outputFile))
        {
            process.StandardOutput.BaseStream.CopyTo(file);
        }
        process.WaitForExit();
        var stderr = stderrTask.GetAwaiter().GetResult();
        var ok = process.ExitCode == 0;
        if (!ok)
        {
            log.Warn($"{Path.GetFileName(exe)} exited with {process.ExitCode}: {Truncate(stderr, 300)}");
        }
        return ok;
    }

    /// <summary>Feeds lines to a tool on stdin and captures stdout.</summary>
    public static string RunWithInput(string exe, string arguments, string input, Logger log)
    {
        var info = new ProcessStartInfo(exe, arguments)
        {
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        using var process = Process.Start(info) ?? throw new InvalidOperationException($"cannot start {exe}");
        var stdoutTask = process.StandardOutput.ReadToEndAsync();
        var stderrTask = process.StandardError.ReadToEndAsync();
        process.StandardInput.Write(input);
        process.StandardInput.Close();
        var stdout = stdoutTask.GetAwaiter().GetResult();
        var stderr = stderrTask.GetAwaiter().GetResult();
        process.WaitForExit();
        if (process.ExitCode != 0)
        {
            log.Warn($"{Path.GetFileName(exe)} exited with {process.ExitCode}: {Truncate(stderr, 400)}");
        }
        return stdout;
    }

    public static string Quote(string value) => value.Contains(' ') || value.Contains('"') ? '"' + value.Replace("\"", "\\\"") + '"' : value;

    public static string Truncate(string value, int max) => value.Length <= max ? value : value[..max] + "...";

    public static string Bytes(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB"];
        double value = bytes;
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }
        return $"{value:0.#} {units[unit]}";
    }
}
