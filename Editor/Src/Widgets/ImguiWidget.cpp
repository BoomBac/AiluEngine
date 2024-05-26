#include "Inc/Widgets/ImGuiWidget.h"
#include "Ext/imgui/imgui.h"
#include "Engine/Inc/Framework/Common/Input.h"
#include "Engine/Inc/Render/CommandBuffer.h"
#include "Engine/Inc/Render/Renderer.h"

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
	void ImGuiWidget::SetFocus(const String& title)
	{
		ImGui::SetWindowFocus(title.c_str());
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
	void ImGuiWidget::Close(i32 handle)
	{
		if(handle == _handle)
			_b_show = false;
	}
	void ImGuiWidget::Show()
	{
		if (_b_pre_show != _b_show && _b_show == false)
		{
			for (auto& e : _close_events)
				e();
		}
		_b_pre_show = _b_show;
		if (!_b_show)
		{
			_handle = -1;
			return;
		}
		if(_allow_close)
			ImGui::Begin(_title.c_str(), &_b_show);
		else
			ImGui::Begin(_title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
		_is_focus = ImGui::IsWindowFocused();
		Input::s_block_input |= _is_focus;
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();
		_pos.x = windowPos.x;
		_pos.y = windowPos.y;
		_size.x = windowSize.x;
		_size.y = windowSize.y;
		if (!_is_hide_common_widget_info)
		{
			ImGui::Text("Handle %d,State: pos: (%.2f,%.2f),size: (%.2f,%.2f)", _handle, _pos.x, _pos.y, _size.x, _size.y);
		}
		ShowImpl();
		ImGui::End();
	}
	void ImGuiWidget::ShowImpl()
	{
	}
	void ImGuiWidget::OnWidgetClose(WidgetCloseEvent e)
	{
		_close_events.emplace_back(e);
	}
}
