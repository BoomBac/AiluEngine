#include "Widgets/ImGuiWidget.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"

namespace Ailu
{
	namespace Editor
	{
		void ImGuiWidget::MarkModalWindoShow(const int& handle)
		{
			s_global_modal_window_info[handle] = true;
		}
		String ImGuiWidget::ShowModalDialog(const String& title, int handle)
		{
			if (handle > 255)
			{
				//g_pLogMgr->LogWarningFormat("ImGuiWidget::ShowModalDialog invalid handle : {}", handle);
				return String{};
			}
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
                Input::BlockInput(true);
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
                Input::BlockInput(true);
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
		ImGuiWidget::ImGuiWidget(const String& title) : _title(title), _is_focus(false)
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
			if (handle == _handle)
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
			if (_allow_close)
				ImGui::Begin(_title.c_str(), &_b_show);
			else
				ImGui::Begin(_title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
			_is_focus = ImGui::IsWindowFocused();
			Input::BlockInput(Input::IsInputBlock() || _is_focus);
			ImVec2 windowPos = ImGui::GetWindowPos();
			ImVec2 windowSize = ImGui::GetWindowSize();
			_pos.x = windowPos.x;
			_pos.y = windowPos.y;
            _global_pos = _pos;
			_size.x = windowSize.x;
			_size.y = windowSize.y;
			auto [wx, wy] = Application::GetInstance()->GetWindowPtr()->GetWindowPosition();
			_pos -= Vector2f((f32)wx, (f32)wy);
			if (!_is_hide_common_widget_info)
			{
				ImGui::Text("Handle %d,State: Local pos: (%.2f,%.2f),size: (%.2f,%.2f)", _handle, _pos.x, _pos.y, _size.x, _size.y);
			}
            if (!ImGui::IsWindowCollapsed())
                ShowImpl();
			ImGui::End();
		}

		bool ImGuiWidget::Hover(const Vector2f& mouse_pos) const
		{
			return mouse_pos >= _pos && mouse_pos <= (_pos + _size);
		}
		Vector2f ImGuiWidget::GlobalToLocal(const Vector2f& screen_pos) const
		{
			//auto [wx, wy] = Application::GetInstance()->GetWindowPtr()->GetWindowPosition();
			return screen_pos - _pos;
		}
		void ImGuiWidget::ShowImpl()
		{
		}
		void ImGuiWidget::OnWidgetClose(WidgetCloseEvent e)
		{
			_close_events.emplace_back(e);
		}
        void ImGuiWidget::OnEvent(Event &e)
        {
            if (!Hover(Input::GetMousePos()))
                return ;
            if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                auto& event = static_cast<MouseButtonPressedEvent&>(e);
                if (event.GetButton() == AL_KEY_RBUTTON)
                {
                    ImGui::SetNextWindowFocus();
                }
            }
        }

        void ImGuiWidget::OnObjectDropdown(const String &tag, std::function<void(Object*)> f)
        {
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(tag.c_str());
                if (payload)
                {
                    Object* obj = *(Object **) (payload->Data);
                    LOG_INFO("Object {} drop down...",obj->Name());
                    f(obj);
                }
                ImGui::EndDragDropTarget();
            }
        }
        void ImGuiWidget::DisplayProgressBar(const String &title, f32 progress)
        {
            std::lock_guard<std::mutex> lock(s_progress_lock);
            s_progress_infos[title] = progress;
            if(progress >= 1.0f)
            {
                s_progress_infos.erase(title);
            }
        }
        void ImGuiWidget::ClearProgressBar()
        {
            std::lock_guard<std::mutex> lock(s_progress_lock);
            s_progress_infos.clear();
        }
        void ImGuiWidget::ShowProgressBar()
        {
            std::lock_guard<std::mutex> lock(s_progress_lock);
            if (s_progress_infos.empty())
                return ;
            ImGui::Begin("##Example: ProgressBar");
            for(auto& it : s_progress_infos)
            {
                auto& [title,progress] = it;
                ImGui::Text("%s: ", title.c_str());
                ImGui::SameLine();
                ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
            }
            ImGui::End();
        }
    }
}
