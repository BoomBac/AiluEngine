using AiluMcp;

// --project <path> points at the Ailu project directory; otherwise the session
// file is searched for by walking up from the current working directory.
string? projectPath = null;
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--project" && i + 1 < args.Length)
        projectPath = args[++i];
}

EditorConnection? editor = null;
var session = SessionLocator.Find(projectPath);
if (session is not null)
{
    editor = new EditorConnection(session.PipeName);
    if (!editor.Connect())
    {
        Console.Error.WriteLine($"AiluMcpServer: session found but editor pipe '{session.PipeName}' is not reachable.");
        editor = null;
    }
    else
    {
        Console.Error.WriteLine($"AiluMcpServer: connected to editor pid {session.Pid} ({session.ProjectPath}).");
    }
}
else
{
    Console.Error.WriteLine("AiluMcpServer: no editor session found (looked for .ailu/editor_session.json).");
}

using (editor)
{
    var server = new McpServer(editor);
    server.Run();
}
