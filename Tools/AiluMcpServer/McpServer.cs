using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace AiluMcp;

/// <summary>
/// Minimal MCP stdio server. Reads newline-delimited JSON-RPC messages from
/// stdin, forwards tool calls to the editor over the named pipe and writes
/// JSON-RPC responses to stdout.
/// </summary>
public sealed class McpServer
{
    private readonly EditorConnection? _editor;
    private JsonArray? _tools;

    public McpServer(EditorConnection? editor)
    {
        _editor = editor;
    }

    public void Run()
    {
        Console.InputEncoding = Encoding.UTF8;
        using var stdout = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false))
        {
            NewLine = "\n",
            AutoFlush = true,
        };

        string? line;
        while ((line = Console.ReadLine()) is not null)
        {
            if (string.IsNullOrWhiteSpace(line))
                continue;

            JsonObject? request;
            try
            {
                request = JsonNode.Parse(line)?.AsObject();
            }
            catch (JsonException)
            {
                WriteParseError(stdout);
                continue;
            }
            if (request is null)
                continue;

            var method = request["method"]?.GetValue<string>();
            if (method is null)
                continue; // Inbound response; not expected from the host.

            var id = request["id"];
            if (id is null)
            {
                HandleNotification(method);
                continue;
            }

            JsonObject? result = null;
            JsonObject? error = null;
            try
            {
                switch (method)
                {
                    case "initialize": result = HandleInitialize(); break;
                    case "ping": result = new JsonObject(); break;
                    case "tools/list": result = HandleToolsList(); break;
                    case "tools/call": result = HandleToolsCall(request["params"]?.AsObject()); break;
                    default:
                        error = new JsonObject { ["code"] = -32601, ["message"] = $"method not found: {method}" };
                        break;
                }
            }
            catch (Exception ex)
            {
                error = new JsonObject { ["code"] = -32603, ["message"] = ex.Message };
            }

            var response = new JsonObject { ["jsonrpc"] = "2.0", ["id"] = id?.DeepClone() };
            if (error is not null)
                response["error"] = error;
            else
                response["result"] = result;
            stdout.WriteLine(response.ToJsonString());
        }
    }

    private static void WriteParseError(TextWriter stdout)
    {
        var response = new JsonObject
        {
            ["jsonrpc"] = "2.0",
            ["id"] = null,
            ["error"] = new JsonObject { ["code"] = -32700, ["message"] = "parse error" },
        };
        stdout.WriteLine(response.ToJsonString());
    }

    private static void HandleNotification(string method)
    {
        // notifications/initialized, notifications/cancelled and others need no reply.
        _ = method;
    }

    private static JsonObject HandleInitialize()
    {
        return new JsonObject
        {
            ["protocolVersion"] = "2024-11-05",
            ["capabilities"] = new JsonObject
            {
                ["tools"] = new JsonObject { ["listChanged"] = false },
            },
            ["serverInfo"] = new JsonObject { ["name"] = "ailu-mcp", ["version"] = "0.1.0" },
        };
    }

    private JsonObject HandleToolsList()
    {
        _tools ??= BuildTools();
        return new JsonObject { ["tools"] = _tools?.DeepClone() ?? new JsonArray() };
    }

    private JsonArray BuildTools()
    {
        if (_editor is null || !_editor.Connect())
            return new JsonArray();
        return ToolCatalog.Build(_editor);
    }

    private JsonObject HandleToolsCall(JsonObject? parameters)
    {
        var name = parameters?["name"]?.GetValue<string>() ?? "";
        // Deep-clone: the request node must not be shared between the inbound
        // JSON-RPC message and the editor pipe message.
        var arguments = parameters?["arguments"]?.DeepClone()?.AsObject() ?? new JsonObject();

        if (_editor is null)
        {
            return ToolError("editor_not_connected",
                "The Ailu editor is not reachable. Start the editor with a project open and retry.");
        }

        var response = _editor.Invoke(name, arguments);
        if (response is null)
        {
            return ToolError("transport_error", "Lost connection to the Ailu editor.");
        }

        var success = response["success"]?.GetValue<bool>() ?? false;
        if (success)
        {
            var text = response["data"]?.ToJsonString() ?? "null";
            return new JsonObject
            {
                ["content"] = new JsonArray { new JsonObject { ["type"] = "text", ["text"] = text } },
                ["isError"] = false,
            };
        }

        var code = response["error"]?["code"]?.GetValue<string>() ?? "error";
        var message = response["error"]?["message"]?.GetValue<string>() ?? "unknown error";
        return ToolError(code, message);
    }

    private static JsonObject ToolError(string code, string message)
    {
        return new JsonObject
        {
            ["content"] = new JsonArray { new JsonObject { ["type"] = "text", ["text"] = $"{code}: {message}" } },
            ["isError"] = true,
        };
    }
}
