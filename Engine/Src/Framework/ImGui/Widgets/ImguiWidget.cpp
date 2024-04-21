#include "pch.h"
#include "Framework/ImGui/Widgets/ImGuiWidget.h"
#include "Ext/imgui/imgui.h"

namespace Ailu
{
	void ImGuiWidget::MarkModalWindoShow(const int& handle)
	{
		s_global_modal_window_info[handle] = true;
	}
	String ImGuiWidget::ShowModalDialog(const String& title, int handle)
	{
		static char buf[256] = {};
		String str_id = std::format("{}-{}", title, handle);
		if (s_global_modal_window_info[handle])
		{
			ImGui::OpenPopup(str_id.c_str());
			memset(buf, 0, 256);
			s_global_modal_window_info[handle] = false;
		}
		if (ImGui::BeginPopupModal(str_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter Text:");
			ImGui::InputText("##inputText", buf, IM_ARRAYSIZE(buf));
			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return String{ buf };
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		return String{};
	}
	String ImGuiWidget::ShowModalDialog(const String& title, std::function<void()> show_widget, int handle)
	{
		static char buf[256] = {};
		String str_id = std::format("{}-{}", title, handle);
		if (s_global_modal_window_info[handle])
		{
			ImGui::OpenPopup(str_id.c_str());
			s_global_modal_window_info[handle] = false;
		}
		if (ImGui::BeginPopupModal(str_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text(title.c_str());
			show_widget();
			ImGui::EndPopup();
		}
		return String{};
	}
	ImGuiWidget::ImGuiWidget(const String& title) : _title(title),_is_focus(false)
	{
	}
	ImGuiWidget::~ImGuiWidget()
	{

	}
	void ImGuiWidget::Open(const i32& handle)
	{
		_handle = handle;
		_b_show = true;
	}
	void ImGuiWidget::Close()
	{
		_b_show = false;
	}
	void ImGuiWidget::Show()
	{
		if (!_b_show)
		{
			_handle = -1;
			return;
		}

		ImGui::Begin(std::format("{} ,handle {}",_title,_handle).c_str(), &_b_show);
		_is_focus = ImGui::IsWindowFocused();
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();
		_left_top.x = windowPos.x;
		_left_top.y = windowPos.y;
		_right_bottom.x = windowSize.x;
		_right_bottom.y = windowSize.y;
		ImGui::Text("State: pos: (%.2f,%.2f),size: (%.2f,%.2f)", _left_top.x, _left_top.y, _right_bottom.x, _right_bottom.y);
		ShowImpl();
		ImGui::End();
	}
	void ImGuiWidget::ShowImpl()
	{
	}
}
