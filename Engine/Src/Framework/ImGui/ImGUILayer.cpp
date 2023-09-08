#include "pch.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/TimeMgr.h"

Ailu::ImGUILayer::ImGUILayer()
{
}

Ailu::ImGUILayer::~ImGUILayer()
{
    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
static ImFont* font0 = nullptr;

void Ailu::ImGUILayer::OnAttach()
{
    IMGUI_CHECKVERSION();
    auto context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.MouseDrawCursor = true;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    font0 = io.Fonts->AddFontFromFileTTF(GET_ENGINE_FULL_PATH(Res/Fonts/VictorMono-Regular.ttf), 13.0f);
    io.Fonts->Build();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplWin32_Init(Application::GetInstance()->GetWindow().GetNativeWindowPtr());
}

void Ailu::ImGUILayer::OnDetach()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Ailu::ImGUILayer::OnEvent(Event& e)
{

}

void Ailu::ImGUILayer::OnImguiRender()
{
    ImGui::PushFont(font0);
    ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.
    ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
    ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
    ImGui::End();
    ImGui::PopFont();
    static bool show = true;
    ImGui::ShowDemoWindow(&show);
}

void Ailu::ImGUILayer::Begin()
{
    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Ailu::ImGUILayer::End()
{
    const Window& window = Application::GetInstance()->GetWindow();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));

    ImGui::EndFrame();
    ImGui::Render();
}

