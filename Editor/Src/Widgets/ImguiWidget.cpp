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
        static Vector2f ImVecToVector(const ImVec2 &v)
        {
            return {v.x, v.y};
        }
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
            _pos = ImVecToVector(windowPos);
            _size = ImVecToVector(windowSize);
            //_pos.y -= 30; //app_window_title_bar_height
            //windowPos = ImGui::GetWindowPos();                                                      // 获取当前窗口左上角位置（包括标题栏）
            ImVec2 contentMin = ImGui::GetWindowContentRegionMin();                                 // 获取内容区域的最小值（相对于窗口左上角）
            ImVec2 contentMax = ImGui::GetWindowContentRegionMax();                                 // 获取内容区域的最小值（相对于窗口左上角）
            //ImVec2 contentPos = ImVec2(windowPos.x + contentMin.x, windowPos.y + contentMin.y);     // 内容区域左上角的屏幕坐标
            _content_size = Vector2f(contentMax.x - contentMin.x, contentMax.y - contentMin.y);
            _content_pos = Vector2f(windowPos.x + contentMin.x, windowPos.y + contentMin.y);
            _screen_pos = _pos;

            //{
            //    ImVec2 vMin = ImGui::GetWindowContentRegionMin();
            //    ImVec2 vMax = ImGui::GetWindowContentRegionMax();

            //    vMin.x += ImGui::GetWindowPos().x;
            //    vMin.y += ImGui::GetWindowPos().y;
            //    vMax.x += ImGui::GetWindowPos().x;
            //    vMax.y += ImGui::GetWindowPos().y;
            //    _content_size = Vector2f(vMax.x - vMin.x, vMax.y - vMin.y);
            //    _content_pos = ImVecToVector(vMin) - app_window_pos;
            //    ImGui::GetForegroundDrawList()->AddRect(vMin, vMax, IM_COL32(255, 255, 0, 255));
            //}
			if (!_is_hide_common_widget_info)
			{
                String info = std::format("Handle {},pos: {},size: {},content pos: {},content size: {},screen pos: {},mouse pos {}",_handle,_pos.ToString(),_size.ToString(),_content_pos.ToString(),
					_content_size.ToString(),_screen_pos.ToString(),Input::GetMousePos().ToString());
                ImGui::Text(info.c_str());
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
			return screen_pos - _content_pos;
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

        void ImGuiWidget::OnObjectDropdown(const String &tag, std::function<void(Object *)> f)
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
