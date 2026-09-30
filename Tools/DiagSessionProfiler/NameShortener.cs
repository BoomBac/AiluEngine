using System.Text;

namespace Ailu.DiagSessionProfiler;

/// <summary>Compacts C++ symbol names so the report stays inside a sensible token budget.</summary>
public static class NameShortener
{
    private const int MaxTemplateArgumentLength = 40;
    private const int MaxNameLength = 150;

    private static readonly string[] NoisePrefixes =
    [
        "class ", "struct ", "enum ", "union ", "const ", "volatile ",
        "__cdecl ", "__fastcall ", "__stdcall ", "__thiscall ", "__vectorcall ",
    ];

    public static string Canonicalize(string? name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return "(unknown)";
        }
        var text = name.Trim();
        var changed = true;
        while (changed)
        {
            changed = false;
            foreach (var prefix in NoisePrefixes)
            {
                if (text.Contains(prefix, StringComparison.Ordinal))
                {
                    text = text.Replace(prefix, "");
                    changed = true;
                }
            }
        }

        text = CutParameterList(text);
        text = CollapseTemplates(text);
        if (text.Length > MaxNameLength)
        {
            text = text[..MaxNameLength] + "...";
        }
        return text;
    }

    /// <summary>Drops the parameter list of a function (keeping a trailing operator()).</summary>
    private static string CutParameterList(string text)
    {
        var depth = 0;
        var searchFrom = 0;
        while (true)
        {
            var index = text.IndexOf('(', searchFrom);
            if (index < 0)
            {
                return text;
            }
            depth = 0;
            for (var i = index - 1; i >= 0; i--)
            {
                if (text[i] == '>')
                {
                    depth++;
                }
                else if (text[i] == '<')
                {
                    depth--;
                }
                else if (depth == 0 && text[i] == ')')
                {
                    depth = -1;
                    break;
                }
            }
            if (depth == -1)
            {
                // this '(' belongs to a parameter list we already skipped
                searchFrom = index + 1;
                continue;
            }
            var isOperator = text[..index].EndsWith("operator", StringComparison.Ordinal);
            if (!isOperator)
            {
                return text[..index].TrimEnd();
            }
            var close = text.IndexOf(')', index);
            if (close < 0)
            {
                return text;
            }
            var head = text[..(close + 1)];
            var rest = close + 1 < text.Length ? text[(close + 1)..] : "";
            var nextParen = rest.IndexOf('(');
            return nextParen < 0 ? head : head + rest[..nextParen].TrimEnd();
        }
    }

    /// <summary>Replaces long template argument lists with an ellipsis, innermost first.</summary>
    private static string CollapseTemplates(string text)
    {
        for (var pass = 0; pass < 12; pass++)
        {
            var open = -1;
            var replaced = false;
            var builder = new StringBuilder(text.Length);
            for (var i = 0; i < text.Length; i++)
            {
                var c = text[i];
                if (c == '<')
                {
                    open = i;
                    builder.Append(c);
                    continue;
                }
                if (c == '>' && open >= 0)
                {
                    var inner = text[(open + 1)..i];
                    if (inner.Length > MaxTemplateArgumentLength)
                    {
                        builder.Length -= inner.Length;
                        builder.Append('…');
                        replaced = true;
                    }
                    builder.Append(c);
                    open = -1;
                    continue;
                }
                builder.Append(c);
            }
            text = builder.ToString();
            if (!replaced)
            {
                break;
            }
        }
        return text;
    }

    /// <summary>True when the file belongs to the toolchain rather than to the profiled project.</summary>
    public static bool IsToolchainFile(string? file)
    {
        if (string.IsNullOrEmpty(file))
        {
            return false;
        }
        var lower = file.ToLowerInvariant();
        return lower.Contains(@"\vc\tools\") || lower.Contains(@"\windows kits\") || lower.Contains(@"\llvm\")
            || lower.Contains(@"\msvc\") || lower.Contains(@"\ucrt\") || lower.Contains(@"\glm\")
            || lower.Contains(@"\imgui\") || lower.Contains(@"\directxtex\") || lower.Contains(@"\box2d\");
    }
}
