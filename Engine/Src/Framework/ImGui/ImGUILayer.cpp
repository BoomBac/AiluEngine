#include "pch.h"
#include "Framework/ImGui/ImGuiLayer.h"
#include "Framework/Common/ResourceMgr.h"
#include "Platform/WinWindow.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_win32.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Ext/imgui/imgui_internal.h"
#include "Ext/ImGuizmo/ImGuizmo.h"
#include "Ext/implot/implot.h"
#include "Ext/imnodes/imnodes.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/TimeMgr.h"


namespace Ailu
{
	static int cur_object_index = 0u;
	static u32 cur_tree_node_index = 0u;
	static u16 s_mini_tex_size = 64;

	namespace TreeStats
	{
		static ImGuiTreeNodeFlags s_base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		static int s_selected_mask = 0;
		static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
		static int s_node_clicked = -1;
		static u32 s_pre_frame_selected_actor_id = 10;
		static u32 s_cur_frame_selected_actor_id = 0;
		static i16 s_cur_opened_texture_selector = -1;
		static bool s_is_texture_selector_open = true;
		static u32 s_common_property_handle = 0u;
		static String s_texture_selector_ret = "none";
	}


	Ailu::ImGUILayer::ImGUILayer() : ImGUILayer("ImGUILayer")
	{
	}

	ImGUILayer::ImGUILayer(const String& name) : Layer(name)
	{
		IMGUI_CHECKVERSION();
		ImGui::SetCurrentContext(ImGui::CreateContext());
        ImPlot::SetCurrentContext(ImPlot::CreateContext());
        ImNodes::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		//io.MouseDrawCursor = true;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
        if (!Application::Get()._is_multi_thread_rendering.load())
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
            _is_viewports_active = true;
        }
		io.ConfigWindowsMoveFromTitleBarOnly = true;
		ImGuiStyle& style = ImGui::GetStyle();
		// Setup Dear ImGui style
		ImGui::StyleColorsDark(&style);
		style.WindowPadding = ImVec2(2.0f, 2.0f);
		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.ChildRounding = 0.0f;
			style.FrameRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 0.8f;
		}
        //_font = io.Fonts->AddFontFromFileTTF(ResourceMgr::GetResSysPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
		auto font_path = ResourceMgr::GetResSysPath(L"Fonts/Open_Sans/static/OpenSans-Regular.ttf");
        io.Fonts->AddFontFromFileTTF(ToChar(font_path).c_str(), 14.0f);
        io.Fonts->Build();
        style.WindowMinSize.y = 16.0f;
        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.99f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.25f, 0.25f, 0.25f, 0.54f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.71f, 0.34f, 0.74f, 0.40f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.71f, 0.34f, 0.74f, 0.68f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.47f, 0.71f, 0.58f, 0.86f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.47f, 0.71f, 0.58f, 0.86f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.98f, 0.26f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.88f, 0.85f, 0.28f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.24f, 0.88f, 0.85f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.66f, 0.33f, 0.69f, 0.43f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.71f, 0.34f, 0.74f, 0.72f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.71f, 0.34f, 0.74f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.33f, 0.48f, 0.40f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.71f, 0.34f, 0.74f, 0.72f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.33f, 0.48f, 0.40f, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.47f, 0.71f, 0.58f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.47f, 0.71f, 0.58f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.47f, 0.71f, 0.58f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_TabSelected]            = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_TabSelectedOverline]    = ImVec4(0.43f, 0.55f, 0.70f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_TabDimmed]              = ImVec4(0.33f, 0.48f, 0.40f, 0.31f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.98f, 0.71f, 0.70f);
        ImGui_ImplWin32_Init(Application::Get().GetWindow().GetNativeWindowPtr());
	}

	Ailu::ImGUILayer::~ImGUILayer()
	{
		// Cleanup
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
        ImNodes::DestroyContext();
        ImPlot::DestroyContext();
		ImGui::DestroyContext();
	}

	void Ailu::ImGUILayer::OnAttach()
	{

	}

	void Ailu::ImGUILayer::OnDetach()
	{

	}

	void Ailu::ImGUILayer::OnEvent(Event& e)
	{

	}
	static bool show = false;
	static bool s_show_asset_table = false;
	static bool s_show_rt = false;
	static bool s_show_renderview = true;

	void Ailu::ImGUILayer::OnImguiRender()
	{

	}

	void Ailu::ImGUILayer::Begin()
	{
        ApplyViewportConfig();
		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
        ApplyViewportConfig();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
	}

    void Ailu::ImGUILayer::ApplyViewportConfig()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (Application::Get()._is_multi_thread_rendering.load())
        {
            if (_is_viewports_active)
            {
                io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
                ImGui::DestroyPlatformWindows();
                _is_viewports_active = false;
            }
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
            return;
        }

        if (!_is_viewports_active)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            _is_viewports_active = true;
        }
    }

	void Ailu::ImGUILayer::End()
	{
		const Window& window = Application::Get().GetWindow();
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));
		//ImGuiWidget::EndFrame();
        RefreshEngineInputCapture();
		ImGui::EndFrame();
		ImGui::Render();
	}

    bool Ailu::ImGUILayer::ShouldBlockEngineInputEvent(const Event &e) const
    {
        return false;
    }

    void Ailu::ImGUILayer::RefreshEngineInputCapture()
    {
        ImGuiIO &io = ImGui::GetIO();
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
        const bool item_hovered = ImGui::IsAnyItemHovered();
        const bool item_active = ImGui::IsAnyItemActive();
        _blocks_engine_mouse_input = io.WantCaptureMouse || hovered || item_hovered || item_active;
        _blocks_engine_keyboard_input = io.WantCaptureKeyboard || io.WantTextInput || item_active;
        if (_blocks_engine_mouse_input)
            InputRouteState::Get().Consume(InputChannel::kMouse);
        if (_blocks_engine_keyboard_input)
            InputRouteState::Get().Consume(InputChannel::kKeyboard);
        if (io.WantTextInput)
            InputRouteState::Get().Consume(InputChannel::kText);
    }
}


