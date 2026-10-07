# M0 — Plugin Scaffold + Loopback Transport

# Purpose

在不触碰 Scene / Mesh / Material 的情况下，验证现有插件系统能够安全承载 Blender Live Link 的网络生命周期。

本阶段完成后必须证明：

```text
Ailu Editor
    ↓
autoload BlenderLiveLink plugin
    ↓
127.0.0.1 TCP server
    ↓
Blender/test client connect
    ↓
HELLO / PING
    ↓
Disconnect
    ↓
Plugin unload
    ↓
worker joined + socket closed
```

---

# Scope

实现：

- `Plugins/BlenderLiveLink` DLL scaffold。
- `plugin.json`。
- `LiveLinkProtocol` header。
- loopback-only TCP server。
- 单 client。
- background receive/send worker。
- command queue 基础。
- Hello / HelloAck。
- Ping / Pong。
- protocol version mismatch。
- payload size guard。
- active client 下安全 plugin unload。
- Blender Add-on 最小 Connect/Disconnect UI 或 Python smoke client。

---

# Non-goals

本阶段不实现：

- Scene entity。
- Transform。
- Mesh。
- Material。
- Texture。
- Light。
- Camera。
- full sync scene semantics。
- Editor UI。
- multiple clients。
- reconnect recovery。
- file transfer。

---

# Architecture Constraints

## Plugin OnLoad

只允许：

```text
create LiveLinkServer
bind 127.0.0.1
start worker
```

禁止访问 ActiveScene。

## Network Worker

只能操作：

```text
socket
packet buffer
thread-safe queue
atomic state
```

禁止使用 Engine Scene/Render。

## Shutdown

必须：

```text
stop flag
shutdown socket
close client
close listen socket
join thread
clear queue
```

所有步骤在 `OnUnload()` 返回前完成。

---

# Baseline

已有：

```text
PluginManager
Manifest
Autoload
Plugin Tick
Safe Unload
RegistrationScope
```

本阶段不改 `kPluginApiVersion`。

---

# Target Outcome

Ailu log：

```text
[BlenderLiveLink] Server listening on 127.0.0.1:17777
[BlenderLiveLink] Client connected
[BlenderLiveLink] Protocol v1 accepted
[BlenderLiveLink] Client disconnected
[BlenderLiveLink] Server stopped
```

---

# Relevant Files

新增：

```text
Plugins/BlenderLiveLink/CMakeLists.txt
Plugins/BlenderLiveLink/plugin.json

Plugins/BlenderLiveLink/Inc/BlenderLiveLinkPlugin.h
Plugins/BlenderLiveLink/Inc/LiveLinkProtocol.h
Plugins/BlenderLiveLink/Inc/LiveLinkServer.h
Plugins/BlenderLiveLink/Inc/LiveLinkCommandQueue.h

Plugins/BlenderLiveLink/Src/BlenderLiveLinkPlugin.cpp
Plugins/BlenderLiveLink/Src/LiveLinkProtocol.cpp
Plugins/BlenderLiveLink/Src/LiveLinkServer.cpp
```

参考：

```text
Engine/Inc/Framework/Plugin/PluginApi.h
Engine/Inc/Framework/Plugin/PluginManager.h
Engine/Src/Framework/Plugin/PluginManager.cpp
Editor/Src/EditorApp.cpp
```

---

# Milestones

当前：

```text
M0
```

下一阶段：

```text
M1 — Session Identity + Transient Scene + Transform
```

---

# Current Milestone

M0。

---

# Implementation

## 0.1 Plugin Manifest

开发期推荐 project/plugin root 下：

```json
{
    "id": "com.ailu.blender_livelink",
    "name": "Blender Live Link",
    "version": "0.1.0",
    "api_version": 1,
    "type": "editor",
    "library": "AiluBlenderLiveLink.dll",
    "autoload": true
}
```

---

## 0.2 Plugin Class

```cpp
class BlenderLiveLinkPlugin final : public IPlugin
{
public:
    bool OnLoad(const PluginContext& context) noexcept override;
    void OnUnload() noexcept override;
    void Tick(float delta_time) noexcept override;

private:
    const PluginContext* _context = nullptr;
    Scope<LiveLinkServer> _server;
};
```

`Tick()` 当前只：

```text
drain diagnostic commands
update connection state
```

不碰 Scene。

---

## 0.3 Header

```cpp
struct LiveLinkPacketHeader
{
    u32 _magic;
    u16 _version;
    u16 _type;
    u32 _payload_size;
    u64 _sequence;
};
```

不能直接把该 struct `send(sizeof(struct))` 后默认认为跨编译器 packing 永远一致。

必须：

- 明确 `static_assert` size。
- 或手工 encode/decode fixed fields。
- protocol little-endian。

推荐手工 encode/decode，避免 packing 风险。

---

## 0.4 Server Thread

第一版一个 thread 足够：

```text
bind/listen
   ↓
accept
   ↓
read exact header
   ↓
validate
   ↓
read exact payload
   ↓
process transport-level message / push queue
```

不需要 IOCP。

---

## 0.5 Max Payload

```cpp
inline constexpr u32 kMaxLiveLinkPayloadSize = 256u * 1024u * 1024u;
```

读取 header 后先验证。

不要：

```cpp
Vector<u8> payload(header._payload_size);
```

之后才检查。

---

## 0.6 Single Client

新的 Blender client 连接时：

MVP 可以：

```text
如果已有 client:
    拒绝新连接
```

不要做多 session 并行。

---

## 0.7 Test Client

优先先写最小 Python smoke script：

```text
connect
send HELLO
receive ACK
send PING
receive PONG
close
```

之后再包进 Blender UI。

这样能把：

```text
transport bug
```

与：

```text
bpy bug
```

分开。

---

# Progress

- [x] plugin target 源码实现并由 `build-debug` 编译。
- [ ] manifest 被 Editor 扫描。
- [ ] Editor autoload 成功。
- [ ] Editor 运行时 server bind loopback。
- [ ] live HELLO/HELLO_ACK。
- [ ] live version mismatch。
- [ ] live PING/PONG 1000 次。
- [ ] live malformed payload cases。
- [ ] live client disconnect。
- [ ] active client 下 plugin unload。
- [ ] unload 时 worker join。
- [ ] DLL unload 后端口可立即重新 bind。

源码实现包含上述 transport 和生命周期路径；未运行 Editor、smoke client 或卸载验收时，不将这些运行时项目标为通过。

---

# Decision Log

- 使用 TCP，不使用 WebSocket。
- 使用 loopback-only。
- 单 client。
- 协议自己 framing。
- control payload 后续使用 JSON。
- Mesh 后续使用 binary。
- OnLoad 不访问 Scene。

---

# Surprises & Discoveries

如果 Windows socket shutdown 导致 worker 不能可靠退出，必须记录实际原因。

不要通过：

```text
detach thread
sleep 100ms
```

规避。

---

# Validation

必须实际执行：

1. 启动 Editor。
2. 确认插件 autoload。
3. Python client HELLO。
4. PING/PONG 1000 次。
5. client 突然 kill。
6. 重新连接。
7. client 保持连接时卸载插件。
8. 确认 Editor 不 crash/hang。
9. 再次加载 plugin。
10. 确认 port 可重新 bind。

额外 malformed test：

```text
wrong magic
wrong version
payload_size > max
truncated payload
```

都必须只断 client，不 crash Editor。

---

# Known Follow-ups

- stats。
- reconnect policy。
- compression。
- multi-client。
- Editor UI。

---

# Outcomes

M0 完成后，Live Link 最危险的 DLL + thread + socket 生命周期已经经过验证。

---

# Resume Protocol

继续前：

1. 读 `../progress.md`。
2. 检查 M0 checklist。
3. 从第一个未完成项继续。
4. 不实现 Scene。
5. 所有 validation 实际运行后写入 progress。
