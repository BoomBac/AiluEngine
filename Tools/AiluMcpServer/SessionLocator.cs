using System.Text.Json;
using System.Text.Json.Nodes;

namespace AiluMcp;

/// <summary>
/// Locates the running editor through the session file that the editor writes at
/// <c>&lt;project&gt;/.ailu/editor_session.json</c>.
/// </summary>
public sealed class EditorSession
{
    public int Pid { get; init; }
    public string SessionId { get; init; } = "";
    public string PipeName { get; init; } = "";
    public string ProjectPath { get; init; } = "";
    public string EditorVersion { get; init; } = "";
}

public static class SessionLocator
{
    /// <summary>
    /// Find the session file. <paramref name="projectPath"/> may be the project
    /// directory, the project file (.ailuproject / *.json), or null to walk up
    /// from the current working directory.
    /// </summary>
    public static EditorSession? Find(string? projectPath)
    {
        List<DirectoryInfo> candidates = new();
        if (!string.IsNullOrEmpty(projectPath))
        {
            // Accept both "C:/proj/MyProject" and "C:/proj/MyProject.ailuproject".
            string path = projectPath;
            if (File.Exists(path))
                path = Path.GetDirectoryName(path) ?? path;
            var dir = new DirectoryInfo(path);
            while (dir is not null)
            {
                candidates.Add(dir);
                dir = dir.Parent;
            }
        }
        else
        {
            var current = new DirectoryInfo(Directory.GetCurrentDirectory());
            while (current is not null)
            {
                candidates.Add(current);
                current = current.Parent;
            }
        }

        foreach (var dir in candidates)
        {
            var file = Path.Combine(dir.FullName, ".ailu", "editor_session.json");
            if (!File.Exists(file))
                continue;
            try
            {
                var node = JsonNode.Parse(File.ReadAllText(file))?.AsObject();
                if (node is null)
                    continue;
                var pipeName = node["pipe_name"]?.GetValue<string>() ?? "";
                if (string.IsNullOrEmpty(pipeName))
                    continue;
                return new EditorSession
                {
                    Pid = node["pid"]?.GetValue<int>() ?? 0,
                    SessionId = node["session_id"]?.GetValue<string>() ?? "",
                    PipeName = pipeName,
                    ProjectPath = node["project_path"]?.GetValue<string>() ?? "",
                    EditorVersion = node["editor_version"]?.GetValue<string>() ?? "",
                };
            }
            catch (JsonException)
            {
                // Ignore malformed session files and keep looking.
            }
        }
        return null;
    }
}
