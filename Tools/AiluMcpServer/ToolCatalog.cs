using System.Text.Json;
using System.Text.Json.Nodes;

namespace AiluMcp;

/// <summary>
/// Builds MCP tool schemas from the editor's <c>automation.list_methods</c>
/// response so the editor stays the single source of truth for tool metadata.
/// </summary>
public static class ToolCatalog
{
    public static JsonArray Build(EditorConnection editor)
    {
        var tools = new JsonArray();
        var response = editor.Invoke("automation.list_methods", null);
        if (response is null || (response["success"]?.GetValue<bool>() ?? false) == false)
            return tools;

        var methods = response["data"]?["methods"]?.AsArray();
        if (methods is null)
            return tools;

        foreach (var methodNode in methods)
        {
            var method = methodNode?.AsObject();
            if (method is null)
                continue;
            var tool = new JsonObject
            {
                ["name"] = method["name"]?.GetValue<string>() ?? "",
                ["description"] = method["description"]?.GetValue<string>() ?? "",
                ["inputSchema"] = BuildInputSchema(method["input_schema"]?.AsArray()),
            };
            tools.Add(tool);
        }
        return tools;
    }

    private static JsonObject BuildInputSchema(JsonArray? paramsArray)
    {
        var properties = new JsonObject();
        var required = new JsonArray();

        if (paramsArray is not null)
        {
            foreach (var paramNode in paramsArray)
            {
                var param = paramNode?.AsObject();
                if (param is null)
                    continue;
                var name = param["name"]?.GetValue<string>() ?? "";
                if (name.Length == 0)
                    continue;
                var schema = new JsonObject
                {
                    ["description"] = param["description"]?.GetValue<string>() ?? "",
                };
                var type = param["type"]?.GetValue<string>() ?? "string";
                var isArray = param["is_array"]?.GetValue<bool>() ?? false;
                ApplyJsonType(schema, type, isArray);
                if (param["enum_values"]?.AsArray() is { Count: > 0 } enumValues)
                {
                    var values = new JsonArray();
                    foreach (var value in enumValues)
                        values.Add(value?.GetValue<string>());
                    schema["enum"] = values;
                }
                properties[name] = schema;
                if (param["required"]?.GetValue<bool>() ?? false)
                    required.Add(name);
            }
        }

        var inputSchema = new JsonObject
        {
            ["type"] = "object",
            ["properties"] = properties,
        };
        if (required.Count > 0)
            inputSchema["required"] = required;
        return inputSchema;
    }

    private static void ApplyJsonType(JsonObject schema, string automationType, bool isArray)
    {
        string jsonType = automationType switch
        {
            "integer" => "integer",
            "float" => "number",
            "bool" => "boolean",
            "vector" or "quaternion" or "color" or "array" => "array",
            "object" => "object",
            _ => "string", // string / guid / enum / null
        };
        if (isArray)
        {
            schema["type"] = "array";
            schema["items"] = new JsonObject { ["type"] = jsonType };
        }
        else
        {
            schema["type"] = jsonType;
        }
    }
}
