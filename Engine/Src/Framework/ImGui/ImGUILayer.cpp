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
    ImGuiStyle& style = ImGui::GetStyle();
    // Setup Dear ImGui style
    ImGui::StyleColorsDark(&style);
    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 10.0f;
        style.ChildRounding = 10.0f;
        style.FrameRounding = 10.0f;
        style.Colors[ImGuiCol_WindowBg].w = 0.8f;
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
static bool show = false;
void Ailu::ImGUILayer::OnImguiRender()
{
    ImGui::PushFont(font0);
    ImGui::Begin("Performace Statics");                          // Create a window called "Hello, world!" and append into it.
    ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
    ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
    ImGui::SliderFloat("Game Time Scale:", &TimeMgr::TimeScale, 0.0f, 0.2f);
    ImGui::Checkbox("Expand", &show);
    ImGui::End();
    ImGui::PopFont();

    if(show) ImGui::ShowDemoWindow(&show);
    
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

