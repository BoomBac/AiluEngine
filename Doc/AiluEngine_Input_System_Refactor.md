# AiluEngine 输入系统重构方案

## 背景

当前 AiluEngine 存在 Raw Input、事件系统、旧 Input 轮询、新 InputSystem
多套输入路径并存的问题。

目标：

-   Raw Input 只表示真实设备状态。
-   UI 负责事件路由与消费。
-   InputSystem 负责 Action / Context。
-   删除全局 BlockInput。
-   Editor/Game 通过 Action 获取输入。

## 重构核心

架构：

    Platform
        |
    Raw Input State
        |
    Input Event Router
        |
    +-----------+
    |           |
    UI      InputSystem
    |           |
    Widgets  Actions
    |           |
    +-----------+
        |
    Editor / Gameplay

## 主要修改

### 1. Raw Input Layer

Input 只维护：

-   键盘状态
-   鼠标状态
-   手柄状态

禁止：

-   UI 修改 Input 状态
-   ImGui 修改 Input 状态
-   Editor 判断 Input 是否被屏蔽

------------------------------------------------------------------------

### 2. UI Input Router

增加：

-   Keyboard Focus
-   Mouse Capture
-   Hover State
-   Modal Root

键盘事件禁止根据鼠标 Hover 路由。

------------------------------------------------------------------------

### 3. Input Context

使用 Context 控制输入优先级：

    Modal
    Text Editing
    Menu
    Gizmo
    SceneView
    Editor Shortcut
    Gameplay

不同 Context 消费不同 Input Channel。

------------------------------------------------------------------------

### 4. ImGui 接入

ImGui 不再调用全局 BlockInput。

改为：

-   Mouse Context
-   Keyboard Context
-   Text Context

鼠标捕获不影响键盘。

------------------------------------------------------------------------

## 迁移阶段

### Phase 1

修复事件链：

-   UI handled 同步 Event handled。
-   增加 Keyboard Focus。
-   增加 Mouse Capture。
-   删除 UILayer 中 BlockInput 重置。

### Phase 2

替换：

    BlockInput
    BlockInputByImGui
    IsInputBlock

改为：

    InputChannel
    InputRouteState

### Phase 3

接入 InputSystem：

-   Application 持有 InputSystem。
-   WinInputBackend 接管平台输入。
-   Action 成为业务接口。

### Phase 4

迁移：

-   SceneView
-   Gizmo
-   Camera Controller
-   Editor Shortcut

删除：

-   IsInputBlock
-   \_is_camera_input_active
-   业务层 Input::IsKeyDown

------------------------------------------------------------------------

## 验证标准

UI：

-   TextBox 输入正常。
-   Popup 不穿透。
-   Modal 阻止背景操作。

SceneView：

-   RMB Camera Look 正常。
-   Mouse Capture 后移出窗口仍正常。
-   Gizmo Drag 不丢失。

最终目标：

Raw Input -\> Router -\> Context -\> Action -\> Editor/Game
