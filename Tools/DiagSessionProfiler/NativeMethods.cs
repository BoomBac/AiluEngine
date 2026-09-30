using System.Runtime.InteropServices;
using System.Text;

namespace Ailu.DiagSessionProfiler;

/// <summary>Thin wrapper around a specific dbghelp.dll (the Windows Kits copy ships symsrv, so prefer it).</summary>
public sealed class DbgHelp : IDisposable
{
    private const uint SymoptLoadLines = 0x10;
    private const uint SymoptUndName = 0x02;

    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    private delegate bool SymInitializeDelegate(IntPtr process, string? searchPath, bool invade);
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    private delegate ulong SymLoadModuleExDelegate(IntPtr process, IntPtr file, string imageName, string? moduleName, ulong baseOfDll, uint dllSize, IntPtr data, uint flags);
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    private delegate bool SymFromAddrDelegate(IntPtr process, ulong address, out ulong displacement, IntPtr symbolInfo);
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Unicode)]
    private delegate bool SymGetLineFromAddrDelegate(IntPtr process, ulong address, out uint displacement, IntPtr lineInfo);
    private delegate bool SymCleanupDelegate(IntPtr process);
    private delegate uint SymSetOptionsDelegate(uint options);
    private delegate int UnDecorateSymbolNameDelegate(string name, StringBuilder output, int maxLength, uint flags);

    private readonly IntPtr _library;
    private readonly SymInitializeDelegate _initialize;
    private readonly SymLoadModuleExDelegate _loadModule;
    private readonly SymFromAddrDelegate _fromAddr;
    private readonly SymGetLineFromAddrDelegate _lineFromAddr;
    private readonly SymCleanupDelegate _cleanup;
    private readonly UnDecorateSymbolNameDelegate _undecorate;
    private readonly IntPtr _process = new(0x5150);

    public DbgHelp(string dbghelpPath)
    {
        _library = NativeLibrary.Load(dbghelpPath);
        _initialize = Marshal.GetDelegateForFunctionPointer<SymInitializeDelegate>(NativeLibrary.GetExport(_library, "SymInitializeW"));
        _loadModule = Marshal.GetDelegateForFunctionPointer<SymLoadModuleExDelegate>(NativeLibrary.GetExport(_library, "SymLoadModuleExW"));
        _fromAddr = Marshal.GetDelegateForFunctionPointer<SymFromAddrDelegate>(NativeLibrary.GetExport(_library, "SymFromAddrW"));
        _lineFromAddr = Marshal.GetDelegateForFunctionPointer<SymGetLineFromAddrDelegate>(NativeLibrary.GetExport(_library, "SymGetLineFromAddrW64"));
        _cleanup = Marshal.GetDelegateForFunctionPointer<SymCleanupDelegate>(NativeLibrary.GetExport(_library, "SymCleanup"));
        _undecorate = Marshal.GetDelegateForFunctionPointer<UnDecorateSymbolNameDelegate>(NativeLibrary.GetExport(_library, "UnDecorateSymbolName"));
        var setOptions = Marshal.GetDelegateForFunctionPointer<SymSetOptionsDelegate>(NativeLibrary.GetExport(_library, "SymSetOptions"));
        setOptions(SymoptLoadLines | SymoptUndName);
    }

    public bool Initialize(string searchPath) => _initialize(_process, searchPath, false);

    public ulong LoadModule(string imagePath, string moduleName, ulong baseAddress)
        => _loadModule(_process, IntPtr.Zero, imagePath, moduleName, baseAddress, 0, IntPtr.Zero, 0);

    public (string? Function, string? File, int Line) Resolve(ulong address)
    {
        var buffer = Marshal.AllocHGlobal(8192);
        try
        {
            for (var i = 0; i < 8192; i++)
            {
                Marshal.WriteByte(buffer, i, 0);
            }
            Marshal.WriteInt32(buffer, 0, 88);   // sizeof(SYMBOL_INFO)
            Marshal.WriteInt32(buffer, 80, 2000); // MaxNameLen (characters)
            if (!_fromAddr(_process, address, out var displacement, buffer))
            {
                return (null, null, 0);
            }
            var name = Marshal.PtrToStringUni(new IntPtr(buffer.ToInt64() + 84));
            if (string.IsNullOrEmpty(name))
            {
                return (null, null, 0);
            }
            var text = displacement > 0 ? $"{name}+0x{displacement:x}" : name;

            var lineBuffer = Marshal.AllocHGlobal(128);
            try
            {
                for (var i = 0; i < 128; i++)
                {
                    Marshal.WriteByte(lineBuffer, i, 0);
                }
                if (_lineFromAddr(_process, address, out _, lineBuffer))
                {
                    var filePointer = Marshal.ReadIntPtr(lineBuffer, 8);
                    var line = Marshal.ReadInt32(lineBuffer, 16);
                    var file = filePointer == IntPtr.Zero ? null : Marshal.PtrToStringUni(filePointer);
                    return (text, file, line);
                }
            }
            finally
            {
                Marshal.FreeHGlobal(lineBuffer);
            }
            return (text, null, 0);
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    /// <summary>Undecorates an MSVC mangled name into a readable name without parameters.</summary>
    public string Demangle(string mangled)
    {
        if (!mangled.StartsWith('?'))
        {
            return mangled;
        }
        var builder = new StringBuilder(1024);
        var written = _undecorate(mangled, builder, builder.Capacity, 0x1000 /* UNDNAME_NAME_ONLY */);
        return written > 0 ? builder.ToString() : mangled;
    }

    public void Dispose()
    {
        try { _cleanup(_process); } catch { /* ignore */ }
        NativeLibrary.Free(_library);
    }
}

/// <summary>PE helpers: image base, section table, RSDS debug directory patching.</summary>
public static class PeImage
{
    public sealed class Section
    {
        public string Name = "";
        public uint VirtualAddress;
        public uint VirtualSize;
    }

    public static ulong ReadImageBase(byte[] image, int peOffset, ushort magic)
        => magic == 0x20B
            ? BitConverter.ToUInt64(image, peOffset + 24 + 24)
            : BitConverter.ToUInt32(image, peOffset + 24 + 28);

    public static (int PeOffset, ushort Magic, int OptionalHeaderOffset)? ReadHeaders(byte[] image)
    {
        if (image.Length < 0x40 || image[0] != (byte)'M' || image[1] != (byte)'Z')
        {
            return null;
        }
        var peOffset = BitConverter.ToInt32(image, 0x3C);
        if (peOffset <= 0 || peOffset + 24 >= image.Length)
        {
            return null;
        }
        var magic = BitConverter.ToUInt16(image, peOffset + 24);
        return (peOffset, magic, peOffset + 24);
    }

    public static List<Section> ReadSections(byte[] image)
    {
        var result = new List<Section>();
        var headers = ReadHeaders(image);
        if (headers is null)
        {
            return result;
        }
        var (peOffset, _, _) = headers.Value;
        var count = BitConverter.ToUInt16(image, peOffset + 6);
        var optionalSize = BitConverter.ToUInt16(image, peOffset + 20);
        var sectionOffset = peOffset + 24 + optionalSize;
        for (var i = 0; i < count; i++)
        {
            var offset = sectionOffset + i * 40;
            if (offset + 40 > image.Length)
            {
                break;
            }
            result.Add(new Section
            {
                Name = Encoding.ASCII.GetString(image, offset, 8).TrimEnd('\0'),
                VirtualSize = BitConverter.ToUInt32(image, offset + 8),
                VirtualAddress = BitConverter.ToUInt32(image, offset + 12),
            });
        }
        return result;
    }

    /// <summary>Rewrites the CodeView RSDS record so dbghelp and llvm pick up a PDB from another build.</summary>
    public static bool PatchCodeView(string sourcePath, string targetPath, string guidHex, uint age, string pdbFileName)
    {
        var image = File.ReadAllBytes(sourcePath);
        var headers = ReadHeaders(image);
        if (headers is null)
        {
            return false;
        }
        var (peOffset, magic, optionalOffset) = headers.Value;
        var dataDirectoryOffset = optionalOffset + (magic == 0x20B ? 112 : 96);
        var debugRva = BitConverter.ToInt32(image, dataDirectoryOffset + 6 * 8);
        var debugSize = BitConverter.ToInt32(image, dataDirectoryOffset + 6 * 8 + 4);
        if (debugRva <= 0 || debugSize <= 0)
        {
            return false;
        }

        var count = BitConverter.ToUInt16(image, peOffset + 6);
        var optionalSize = BitConverter.ToUInt16(image, peOffset + 20);
        var sectionOffset = peOffset + 24 + optionalSize;
        var debugFileOffset = -1;
        for (var i = 0; i < count; i++)
        {
            var offset = sectionOffset + i * 40;
            var virtualSize = BitConverter.ToUInt32(image, offset + 8);
            var virtualAddress = BitConverter.ToUInt32(image, offset + 12);
            var rawSize = BitConverter.ToUInt32(image, offset + 16);
            var rawPointer = BitConverter.ToUInt32(image, offset + 20);
            if ((uint)debugRva >= virtualAddress && (uint)debugRva < virtualAddress + Math.Max(virtualSize, rawSize))
            {
                debugFileOffset = (int)(rawPointer + (debugRva - virtualAddress));
                break;
            }
        }
        if (debugFileOffset < 0)
        {
            return false;
        }

        var guid = new byte[16];
        for (var i = 0; i < 16; i++)
        {
            guid[i] = Convert.ToByte(guidHex.Substring(i * 2, 2), 16);
        }

        var patched = false;
        for (var entry = 0; entry < debugSize / 28; entry++)
        {
            var offset = debugFileOffset + entry * 28;
            if (offset + 28 > image.Length)
            {
                break;
            }
            var type = BitConverter.ToInt32(image, offset + 12);
            var rawPointer = BitConverter.ToInt32(image, offset + 24);
            if (type != 2 || rawPointer <= 0 || rawPointer + 24 >= image.Length)
            {
                continue;
            }
            if (image[rawPointer] != (byte)'R' || image[rawPointer + 1] != (byte)'S' || image[rawPointer + 2] != (byte)'D' || image[rawPointer + 3] != (byte)'S')
            {
                continue;
            }
            Array.Copy(guid, 0, image, rawPointer + 4, 16);
            Array.Copy(BitConverter.GetBytes(age), 0, image, rawPointer + 20, 4);
            var nameBytes = Encoding.ASCII.GetBytes(pdbFileName);
            Array.Copy(nameBytes, 0, image, rawPointer + 24, nameBytes.Length);
            image[rawPointer + 24 + nameBytes.Length] = 0;
            patched = true;
            break;
        }
        if (!patched)
        {
            return false;
        }
        File.WriteAllBytes(targetPath, image);
        return true;
    }
}

/// <summary>Maps NT device paths (\Device\HarddiskVolumeN\...) to drive letters.</summary>
public static class DevicePathResolver
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int QueryDosDeviceW(string deviceName, StringBuilder targetPath, int maxLength);

    private static readonly Lazy<Dictionary<string, string>> s_deviceToDrive = new(BuildMap);

    private static Dictionary<string, string> BuildMap()
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var letter = 'A'; letter <= 'Z'; letter++)
        {
            var drive = $"{letter}:";
            var buffer = new StringBuilder(1024);
            if (QueryDosDeviceW(drive, buffer, buffer.Capacity) != 0)
            {
                var target = buffer.ToString();
                if (target.StartsWith(@"\Device\", StringComparison.OrdinalIgnoreCase))
                {
                    map[target] = drive;
                }
            }
        }
        return map;
    }

    /// <summary>Resolves a traced image path to a path on this machine, or null when it cannot be mapped.</summary>
    public static string? Resolve(string? path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return null;
        }
        if (path.StartsWith(@"\??\", StringComparison.Ordinal))
        {
            path = path[4..];
        }
        if (path.StartsWith(@"\SystemRoot\", StringComparison.OrdinalIgnoreCase))
        {
            var windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            var candidate = Path.Combine(windows, path[@"\SystemRoot\".Length..]);
            return File.Exists(candidate) ? candidate : null;
        }
        if (path.StartsWith(@"\Device\", StringComparison.OrdinalIgnoreCase))
        {
            foreach (var pair in s_deviceToDrive.Value)
            {
                if (path.StartsWith(pair.Key, StringComparison.OrdinalIgnoreCase))
                {
                    var candidate = pair.Value + path[pair.Key.Length..];
                    return File.Exists(candidate) ? candidate : null;
                }
            }
            return null;
        }
        return File.Exists(path) ? path : null;
    }
}
