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
        if (ImGui::Button("Capture"))
        {
            _cur_frame_data = Profiler::Get().GetLastFrameData();
        }
        // 时间轴参数
        static float scale_factor = 1.0f;   // 缩放系数（影响时间片宽度）
        static float offset_x = 0.0f;        // X轴偏移（拖动调整起始位置）
        static float viewport_width = 800.0f;// 视口宽度
        // 获取可用的窗口大小
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        viewport_width = canvas_size.x;
        ImGui::SliderFloat("Scale", &scale_factor, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("OffsetX", &offset_x, -1.0f,1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
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

        static Vector<String> s_indents = {"","  ","    ","      ","        ","          ","            "};
        if (_cur_frame_data._end_time > 0.0f)
        {
            if (ImGui::TreeNode("Detail"))
            {
                for(auto& it : _cur_frame_data._profile_data_list)
                {
                    auto& [tid,data_list] = it;
                    ImGui::Text("ThreadID: %d", tid);
                    for (auto &itt: data_list)
                    {
                        ImGui::Text("%sName: %s",s_indents[std::min<u16>(itt._call_depth, s_indents.size()-1u)].c_str(), itt._name.c_str());
                        ImGui::SameLine();
                        ImGui::Text("StartTime: %f", itt._start_time);
                        ImGui::SameLine();
                        ImGui::Text("Duration: %f", itt._duration);
                        ImGui::SameLine();
                        ImGui::Text("CallDepth: %d", itt._call_depth);
                    }
                }
                ImGui::TreePop();
            }

            f32 scaled_total_width = viewport_width * scale_factor;
            static f32 s_last_scaled_total_width = scaled_total_width;
            // 获取绘制区域
            ImDrawList *draw_list = ImGui::GetWindowDrawList();
            ImVec2 start_pos = ImGui::GetCursorScreenPos();
            start_pos.x += 20.0f;
            f32 base_y_offset = 25.0f;
            float y_offset = 30.0f;// Y轴初始偏移（行距）

            // 计算 Profile 时间轴范围（自动缩放到数据范围）
            float min_time = _cur_frame_data._start_time;
            float max_time = _cur_frame_data._end_time;
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
            for(auto& it : _cur_frame_data._profile_data_list)
            {
                auto& [tid,data_list] = it;
                u32 id = 0;
                ImGui::Text("ThreadID: %d", tid);
                for (auto& itt : data_list)
                {
                    ProfileFrameData::ProfileEditorData &data = itt;
                    y_offset = 30 + base_y_offset * data._call_depth;
                    f32 cur_time_ratio = data._duration / time_range;
                    f32 x_start = start_pos.x + data._start_time / time_range * scaled_total_width - offset_x;
                    f32 x_end = x_start + cur_time_ratio * scaled_total_width;
                    float y_start = start_pos.y + y_offset;
                    float y_end = y_start + 20.0f;
                    auto c = Random::RandomColor32(id++);
                    // 仅绘制可见区域的时间片（优化）
                    if (x_end >= start_pos.x && x_start <= start_pos.x + viewport_width)
                    {
                        ImU32 color = IM_COL32(c.r * 255, c.g * 255, c.b * 255, 255);
                        draw_list->AddRectFilled(ImVec2(x_start, y_start), ImVec2(x_end, y_end), color);
                        // 显示文本
                        char label[64];
                        snprintf(label, sizeof(label), "%s (%.2f ms)",data._name.c_str(),data._duration);
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
                start_pos.y += 100.0f;
            }
            s_last_scaled_total_width = scaled_total_width;
        }
    }
}// namespace Ailu::Editor