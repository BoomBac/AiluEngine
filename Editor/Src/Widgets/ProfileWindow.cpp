#include "Widgets/ProfileWindow.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Profiler.h"
#include "Framework/Math/Random.hpp"


namespace Ailu::Editor
{
    ProfileWindow::ProfileWindow() : ImGuiWidget("ProfileWindow")
    {
        _allow_close = true;
    }

    ProfileWindow::~ProfileWindow()
    {
    }

    void ProfileWindow::Open(const i32 &handle)
    {
        ImGuiWidget::Open(handle);
    }
    void ProfileWindow::Close(i32 handle)
    {
        ImGuiWidget::Close(handle);
    }
    void ProfileWindow::OnEvent(Event &e)
    {
    }

    void ProfileWindow::ShowImpl()
    {
        ImVec4 origin_child_color = ImGui::GetStyle().Colors[ImGuiCol_ChildBg];
        f32 left_width = 80.0f, top_height = 120.0f;
        // 时间轴参数
        static f32 s_scale_factor = 1.0f;    // 缩放系数（影响时间片宽度）
        static f32 s_offset_x = 0.0f;        // X轴偏移（拖动调整起始位置）
        //上方
        {
            ImGui::BeginChild("Graph", ImVec2(_content_size.x, top_height), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
            //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
            ImGui::Text("Graph");
            static bool s_is_record = false;
            auto origin_btn_c = ImGui::GetStyle().Colors[ImGuiCol_Button];
            ImGui::GetStyle().Colors[ImGuiCol_Button] = s_is_record ? ImVec4(1.0f, 0.1f, 0.1f, 1.0f) : ImVec4(0.1f, 1.0f, 0.1f, 1.0f);
            if (ImGui::Button(s_is_record ? "Stop" : "Start"))
            {
                if (s_is_record)
                {
                    const auto &data = Profiler::Get().GetFrameData();
                    if (!_cached_frame_data.empty())
                    {
                        f32 r = (_view_frame_index - _cached_frame_data.front()._frame_index) / (f32) (_cached_frame_data.back()._frame_index - _cached_frame_data.front()._frame_index);
                        _view_frame_index = (f32) (data.back()._frame_index - data.front()._frame_index) * r + data.front()._frame_index;
                    }
                    _cached_frame_data.clear();
                    _cached_frame_data.reserve(data.size());
                    u32 index = 0u;
                    for (auto it = data.begin(); it != data.end(); ++it)
                    {
                        if (index > 0)
                            AL_ASSERT(_cached_frame_data.back()._end_time > it->_start_time);
                        _cached_frame_data.push_back(*it);
                    }
                }
                s_is_record = !s_is_record;
                if (s_is_record)
                    Profiler::Get().StartRecord();
                else
                    Profiler::Get().StopRecord();
            }
            if (_cached_frame_data.size() > 0)
            {
                u64 frame_start = _cached_frame_data.front()._frame_index;
                u64 frame_end = _cached_frame_data.back()._frame_index;
                u32 old_view_frame_index = _view_frame_index;
                ImGui::SliderScalar("FrameIndex", ImGuiDataType_U64, &_view_frame_index, &frame_start, &frame_end, "%d", ImGuiSliderFlags_AlwaysClamp);
                _view_data_index = (u32) (_view_frame_index - (_view_frame_index > frame_start ? frame_start : 0u));
                if (old_view_frame_index != _view_frame_index)
                    s_offset_x = 0.0f;
            }
            ImGui::GetStyle().Colors[ImGuiCol_Button] = origin_btn_c;
            ImGui::EndChild();
        }
        //下方
        const auto& all_thread = GetAllThreadNameMap();
        {
            ImGui::BeginChild("TimeLineMain", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
            //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            {
                auto main_it = std::find_if(all_thread.begin(),all_thread.end(),[](const auto& it){
                    return it.second == "LogicThread";
                });
                ShowTimeLine(main_it->first,s_offset_x,s_scale_factor);
            }
            ImGui::EndChild();
            ImGui::BeginChild("TimeLineRender", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
            //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            {
                auto render_it = std::find_if(all_thread.begin(),all_thread.end(),[](const auto& it){
                    return it.second == "RenderThread";
                });
                if (render_it != all_thread.end())
                    ShowTimeLine(render_it->first,s_offset_x,s_scale_factor);
            }
            ImGui::EndChild();
        }
        ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = origin_child_color;
    }
    void ProfileWindow::ShowTimeLine(std::thread::id tid,f32& offset_x,f32& scale_factor)
    {
        static float viewport_width = 800.0f;// 视口宽度
        // 获取可用的窗口大小
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        viewport_width = canvas_size.x;
        // ImGui::SliderFloat("Scale", &scale_factor, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        // ImGui::SliderFloat("OffsetX", &offset_x, -1.0f, 1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
        // 允许鼠标滚轮缩放
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
        {
            float zoom_speed = 1.1f;
            if (ImGui::GetIO().MouseWheel > 0.0f)
            {
                scale_factor *= zoom_speed;// 放大
            }
            else
            {
                scale_factor /= zoom_speed;// 缩小
            }
            scale_factor = std::clamp(scale_factor, 0.01f, 100.0f);// 限制缩放范围
        }
        // 允许鼠标拖动平移（按住左键拖动）
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0))
        {
            offset_x -= ImGui::GetIO().MouseDelta.x;// / viewport_width;
        }

        static Vector<String> s_indents = {"", "  ", "    ", "      ", "        ", "          ", "            "};
        if (_cached_frame_data.size() > 0)
        {
            ProfileFrameData &cur_frame_data = _cached_frame_data[_view_data_index];
            f32 scaled_total_width = viewport_width * scale_factor;
            static f32 s_last_scaled_total_width = scaled_total_width;
            // 获取绘制区域
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            f32 base_y_offset = 25.0f;
            float y_offset = 30.0f;// Y轴初始偏移（行距）
            // 计算 Profile 时间轴范围（自动缩放到数据范围）
            float min_time = cur_frame_data._start_time;
            float max_time = cur_frame_data._end_time;
            float time_range = max_time - min_time;   // 总时间跨度
            if (time_range == 0.0f) time_range = 1.0f;// 避免除零
            // 计算当前鼠标在时间轴上的时间点
            ImVec2 mouse_pos = ImGui::GetMousePos();
            f32 mouse_x = mouse_pos.x;
            f32 mouse_x_offseted = mouse_x - start_pos.x + offset_x;
            f32 mouse_time = min_time + mouse_x_offseted / scaled_total_width * time_range;
            mouse_time = std::clamp(mouse_time, min_time, max_time);
            f32 last_mouse_time = min_time + mouse_x_offseted / s_last_scaled_total_width * time_range;
            offset_x = mouse_x_offseted / s_last_scaled_total_width * scaled_total_width - (mouse_x - start_pos.x);
            f32 data_base_time = cur_frame_data._start_time;
            if (_view_data_index == 0 || _view_data_index == _cached_frame_data.size())
                return;
            for (i32 i = -3; i < 3; i++)
            {
                i32 cur_data_index = _view_data_index + i;
                if (cur_data_index < 0 || cur_data_index >= _cached_frame_data.size())
                    continue;
                const auto &cur_data = _cached_frame_data[cur_data_index];
                u32 event_id = 0u;
                for (const auto &itt: cur_data._data_threads.at(tid))
                {
                    const ProfileFrameData::ProfileEditorData &data = itt;
                    y_offset = 30 + base_y_offset * data._call_depth;
                    f32 cur_time_ratio = data._duration / time_range;
                    f32 x_start = start_pos.x + (data._start_time - data_base_time) / time_range * scaled_total_width - offset_x;
                    f32 x_end = x_start + cur_time_ratio * scaled_total_width;
                    f32 y_start = start_pos.y + y_offset;
                    f32 y_end = y_start + 20.0f;
                    auto c = Random::RandomColor(event_id++);
                    if (cur_data._frame_index != _view_frame_index)
                        c.xyz *= 0.5f;
                    if (x_end >= start_pos.x && x_start <= start_pos.x + viewport_width)
                    {
                        ImU32 color = IM_COL32(c.r * 255, c.g * 255, c.b * 255, 255);
                        draw_list->AddRectFilled(ImVec2(x_start, y_start), ImVec2(x_end, y_end), color);
                        // 显示文本
                        char label[64];
                        snprintf(label, sizeof(label), "%s (%.2f ms)", data._name.c_str(), data._duration);
                        auto label_size = ImGui::CalcTextSize(label);
                        if (label_size.x < (x_end - x_start))
                            draw_list->AddText(ImVec2(x_start + 5, y_start + 3), IM_COL32(0, 0, 0, 255), label);
                        if (mouse_pos.x >= x_start && mouse_pos.x <= x_end &&
                            mouse_pos.y >= y_start && mouse_pos.y <= y_end)
                        {
                            // 显示 Tooltip
                            ImGui::BeginTooltip();
                            ImGui::Text("Name: %s", data._name.c_str());
                            ImGui::Text("Start Time: %.4f ms", data._start_time);
                            ImGui::Text("Duration: %.4f ms", data._duration);
                            ImGui::Text("Stop Time: %.4f ms", data._start_time + data._duration);
                            ImGui::EndTooltip();
                        }
                    }
                }
            }

            s_last_scaled_total_width = scaled_total_width;
        }
    }
}// namespace Ailu::Editor
