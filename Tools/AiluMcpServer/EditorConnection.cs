using System.IO.Pipes;
using System.Text;
using System.Text.Json.Nodes;

namespace AiluMcp;

/// <summary>
/// Named-pipe client to the editor. Frames messages as a u32 little-endian
/// length prefix followed by UTF-8 JSON bytes, matching the editor transport.
/// </summary>
public sealed class EditorConnection : IDisposable
{
    private readonly NamedPipeClientStream _pipe;
    private long _nextRequestId = 1;

    public EditorConnection(string pipeName)
    {
        // NamedPipeClientStream expects the bare pipe name without the
        // \\\\.\\pipe\\ prefix that the session file stores.
        const string prefix = @"\\.\pipe\";
        if (pipeName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            pipeName = pipeName.Substring(prefix.Length);
        _pipe = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.None);
    }

    public bool Connect(int timeoutMs = 10000)
    {
        if (_pipe.IsConnected)
            return true;
        try
        {
            _pipe.Connect(timeoutMs);
            _pipe.ReadMode = PipeTransmissionMode.Byte;
            return _pipe.IsConnected;
        }
        catch (TimeoutException)
        {
            return false;
        }
        catch (IOException)
        {
            return false;
        }
    }

    /// <summary>
    /// Send an automation request and return the full response object, or null
    /// when the editor is unreachable.
    /// </summary>
    public JsonObject? Invoke(string method, JsonObject? arguments)
    {
        var request = new JsonObject
        {
            ["request_id"] = _nextRequestId++,
            ["method"] = method,
            ["arguments"] = arguments ?? new JsonObject(),
            ["context"] = new JsonObject
            {
                ["caller"] = "mcp",
                ["allow_write"] = true,
                ["allow_destructive"] = true,
                ["interactive"] = true,
            },
        };
        if (!WriteMessage(request.ToJsonString()))
            return null;
        if (!ReadMessage(out var responseText))
            return null;
        return JsonNode.Parse(responseText)?.AsObject();
    }

    private bool WriteMessage(string json)
    {
        try
        {
            var bytes = Encoding.UTF8.GetBytes(json);
            var length = BitConverter.GetBytes((uint)bytes.Length);
            _pipe.Write(length, 0, length.Length);
            _pipe.Write(bytes, 0, bytes.Length);
            _pipe.Flush();
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (ObjectDisposedException)
        {
            return false;
        }
    }

    private bool ReadMessage(out string json)
    {
        json = "";
        try
        {
            var lengthBytes = new byte[4];
            if (!ReadExact(lengthBytes, 0, lengthBytes.Length))
                return false;
            var length = BitConverter.ToUInt32(lengthBytes, 0);
            if (length > 16 * 1024 * 1024)
                return false;
            var payload = new byte[length];
            if (!ReadExact(payload, 0, payload.Length))
                return false;
            json = Encoding.UTF8.GetString(payload);
            return true;
        }
        catch (IOException)
        {
            return false;
        }
        catch (ObjectDisposedException)
        {
            return false;
        }
    }

    private bool ReadExact(byte[] buffer, int offset, int count)
    {
        int total = 0;
        while (total < count)
        {
            int read = _pipe.Read(buffer, offset + total, count - total);
            if (read == 0)
                return false;
            total += read;
        }
        return true;
    }

    public void Dispose()
    {
        _pipe.Dispose();
    }
}
