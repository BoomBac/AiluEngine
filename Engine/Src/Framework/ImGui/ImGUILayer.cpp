#include "pch.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Platform/WinWindow.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Platform/ImGuiDX12Renderer.h"
#include "Framework/Common/Application.h"

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

void Ailu::ImGUILayer::OnAttach()
{
    IMGUI_CHECKVERSION();
    auto context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(WinWindow::GetWindowHwnd());
}

void Ailu::ImGUILayer::OnDetach()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Ailu::ImGUILayer::OnUpdate()
{
    const Window& window = Application::GetInstance()->GetWindow();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(window.GetWidth(), window.GetHeight());

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

    ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    ImGui::End();

    static bool show = true;
    ImGui::ShowDemoWindow(&show);
    ImGui::EndFrame();
    ImGui::Render();

}

void Ailu::ImGUILayer::OnEvent(Event& e)
{
}
