#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"

#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"
#include "Ext/ImGuizmo/ImGuizmo.h"//必须在imgui之后引入
#include "Ext/imnodes/imnodes.h"
#include "Ext/implot/implot.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/Features/PostprocessPass.h"
#include "Render/Features/VolumetricClouds.h"
#include "Render/Gizmo.h"
#include "Render/RenderingData.h"
#include "UI/TextRenderer.h"
#include "Framework/Common/EngineConfig.h"
//#include <Framework/Common/Application.h>
#include <Objects/Type.h>

#include "Inc/EditorApp.h"

#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"

#include "Framework/Common/Profiler.h"
#include "Render/RenderPipeline.h"

#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "UI/UILayer.h"
#include "UI/UIRenderer.h"
#include "UI/Widget.h"

#include "Objects/JsonArchive.h"

#include "Animation/Curve.hpp"
#include "Animation/Solver.h"
#include "Common/Undo.h"
#include "Framework/Events/KeyEvent.h"
#include "Render/CommonRenderPipeline.h"

#include "Widgets/CommonView.h"
#include "Widgets/RenderView.h"

#include "Framework/Parser/TextParser.h"

namespace Ailu
{
    using namespace Render;
    namespace Editor
    {
        class ProfileWindow
        {
        public:
            ProfileWindow() {};
            ~ProfileWindow() {};
            void Show()
            {
                ShowImpl();
            };
        public:
            Vector2f _content_size;
            bool _is_show = false;
        private:
            void ShowImpl()
            {
                if (!_is_show)
                    return;
                ImGui::Begin("ProfileWindow");
                ImVec4 origin_child_color = ImGui::GetStyle().Colors[ImGuiCol_ChildBg];
                f32 left_width = 80.0f, top_height = 120.0f;
                // 时间轴参数
                static f32 s_scale_factor = 1.0f;// 缩放系数（影响时间片宽度）
                static f32 s_offset_x = 0.0f;    // X轴偏移（拖动调整起始位置）
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
                                _view_frame_index = (u64) ((data.back()._frame_index - data.front()._frame_index) * r + data.front()._frame_index);
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
                        u64 old_view_frame_index = _view_frame_index;
                        ImGui::SliderScalar("FrameIndex", ImGuiDataType_U64, &_view_frame_index, &frame_start, &frame_end, "%d", ImGuiSliderFlags_AlwaysClamp);
                        _view_data_index = (u32) (_view_frame_index - (_view_frame_index > frame_start ? frame_start : 0u));
                        if (old_view_frame_index != _view_frame_index)
                            s_offset_x = 0.0f;
                    }
                    ImGui::DragFloat("s_scale_factor", &s_scale_factor);
                    ImGui::DragFloat("s_offset_x", &s_offset_x);
                    ImGui::GetStyle().Colors[ImGuiCol_Button] = origin_btn_c;
                    ImGui::EndChild();
                }
                //下方
                const auto &all_thread = GetAllThreadNameMap();
                {
                    if (s_engine_config.isMultiThreadRender)
                    {
                        ImGui::BeginChild("TimeLineMain", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                        //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        {
                            auto main_it = std::find_if(all_thread.begin(), all_thread.end(), [](const auto &it)
                                                        { return it.second == "LogicThread"; });
                            ShowTimeLine(main_it->first, s_offset_x, s_scale_factor);
                        }
                        ImGui::EndChild();
                        ImGui::BeginChild("TimeLineRender", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                        //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        {
                            auto render_it = std::find_if(all_thread.begin(), all_thread.end(), [](const auto &it)
                                                          { return it.second == "RenderThread"; });
                            if (render_it != all_thread.end())
                                ShowTimeLine(render_it->first, s_offset_x, s_scale_factor);
                        }
                        ImGui::EndChild();
                    }
                    else
                    {
                        ImGui::BeginChild("TimeLineMain", ImVec2(_content_size.x, 200.0f), ImGuiChildFlags_Border | ImGuiChildFlags_ResizeY);
                        //ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        {
                            auto main_it = std::find_if(all_thread.begin(), all_thread.end(), [](const auto &it)
                                                        { return it.second == "MainThread"; });
                            ShowTimeLine(main_it->first, s_offset_x, s_scale_factor);
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = origin_child_color;
                ImGui::End();
            }
            void ShowTimeLine(std::thread::id tid, f32 &offset_x, f32 &scale_factor)
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

        private:
            Vector<ProfileFrameData> _cached_frame_data;
            u64 _view_frame_index = 0u;
            u32 _view_data_index = 0u;
        };


        void static DrawMemberProperty(PropertyInfo &prop_info, Object &obj)
        {
            ImGui::PushID(obj.Name().c_str());
            auto &meta_info = prop_info.MetaInfo();
            if (!prop_info.IsConst())
            {
                if (prop_info.GetType() == StaticClass<bool>())
                {
                    bool old_value = prop_info.Get<bool>(obj);
                    bool new_value = old_value;
                    if (ImGui::Checkbox(prop_info.Name().c_str(), &new_value))
                    {
                        prop_info.Set<bool>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<f32>())
                {
                    f32 old_value = prop_info.Get<f32>(obj);
                    f32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderFloat(prop_info.Name().c_str(), &new_value, meta_info.GetFloat("RangeMin"), meta_info.GetFloat("RangeMax"));
                    }
                    else
                        ImGui::InputFloat(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<f32>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<i8>())
                {
                    i32 old_value = prop_info.Get<i8>(obj);
                    i32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderInt(prop_info.Name().c_str(), &new_value, (i32) meta_info.GetInt("RangeMin"), (i32) meta_info.GetInt("RangeMax"));
                    }
                    else
                        ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<i8>(obj, (i8) new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<i16>())
                {
                    i32 old_value = prop_info.Get<i16>(obj);
                    i32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderInt(prop_info.Name().c_str(), &new_value, (i32) meta_info.GetInt("RangeMin"), (i32) meta_info.GetInt("RangeMax"));
                    }
                    else
                        ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<i16>(obj, (i16) new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<i32>())
                {
                    i32 old_value = prop_info.Get<i32>(obj);
                    i32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderInt(prop_info.Name().c_str(), &new_value, (i32) meta_info.GetInt("RangeMin"), (i32) meta_info.GetInt("RangeMax"));
                    }
                    else
                        ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<i32>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<u8>())
                {
                    i32 old_value = prop_info.Get<u8>(obj);
                    i32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderInt(prop_info.Name().c_str(), &new_value, meta_info.GetInt("RangeMin") < 0 ? 0 : meta_info.GetInt("RangeMin"), meta_info.GetInt("RangeMax"));
                    }
                    else
                        ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<u8>(obj, (u8) new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<u16>())
                {
                    i32 old_value = prop_info.Get<u16>(obj);
                    i32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderInt(prop_info.Name().c_str(), &new_value, meta_info.GetInt("RangeMin") < 0 ? 0 : meta_info.GetInt("RangeMin"), meta_info.GetInt("RangeMax"));
                    }
                    else
                        ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<u16>(obj, (u16) new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<u32>())
                {
                    i32 old_value = prop_info.Get<u32>(obj);
                    i32 new_value = old_value;
                    if (meta_info.GetBool("IsRange"))
                    {
                        ImGui::SliderInt(prop_info.Name().c_str(), &new_value, meta_info.GetInt("RangeMin") < 0 ? 0 : meta_info.GetInt("RangeMin"), meta_info.GetInt("RangeMax"));
                    }
                    else
                        ImGui::InputInt(prop_info.Name().c_str(), &new_value);
                    if (old_value != new_value)
                    {
                        prop_info.Set<u32>(obj, (u32) new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<String>())
                {
                    String old_value = prop_info.Get<String>(obj);
                    auto str_len = old_value.size();
                    char buf[256];
                    memcpy(buf, old_value.c_str(), str_len);
                    buf[str_len] = '\0';
                    if (ImGui::InputText(prop_info.Name().c_str(), buf, 256, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        prop_info.Set<String>(obj, buf);
                    }
                }
                else if (prop_info.GetType() == StaticClass<Vector2f>())
                {
                    Vector2f old_value = prop_info.Get<Vector2f>(obj);
                    Vector2f new_value = old_value;
                    if (ImGui::InputFloat2(prop_info.Name().c_str(), new_value.data))
                    {
                        prop_info.Set<Vector2f>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<Vector3f>())
                {
                    Vector3f old_value = prop_info.Get<Vector3f>(obj);
                    Vector3f new_value = old_value;
                    if (ImGui::InputFloat3(prop_info.Name().c_str(), new_value.data))
                    {
                        prop_info.Set<Vector3f>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<Vector4f>())
                {
                    Vector4f old_value = prop_info.Get<Vector4f>(obj);
                    Vector4f new_value = old_value;
                    if (meta_info.GetBool("IsColor"))
                    {
                        ImGui::ColorEdit4(prop_info.Name().c_str(), new_value.data, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                        prop_info.Set<Vector4f>(obj, new_value);
                    }
                    else
                    {
                        if (ImGui::InputFloat4(prop_info.Name().c_str(), new_value.data))
                        {
                            prop_info.Set<Vector4f>(obj, new_value);
                        }
                    }
                }
                else if (prop_info.GetType() == StaticClass<Vector2Int>())
                {
                    Vector2Int old_value = prop_info.Get<Vector2Int>(obj);
                    Vector2Int new_value = old_value;
                    if (ImGui::InputInt2(prop_info.Name().c_str(), new_value.data))
                    {
                        prop_info.Set<Vector2Int>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<Vector3Int>())
                {
                    Vector3Int old_value = prop_info.Get<Vector3Int>(obj);
                    Vector3Int new_value = old_value;
                    if (ImGui::InputInt3(prop_info.Name().c_str(), new_value.data))
                    {
                        prop_info.Set<Vector3Int>(obj, new_value);
                    }
                }
                else if (prop_info.GetType() == StaticClass<Vector4Int>())
                {
                    Vector4Int old_value = prop_info.Get<Vector4Int>(obj);
                    Vector4Int new_value = old_value;
                    if (ImGui::InputInt4(prop_info.Name().c_str(), new_value.data))
                    {
                        prop_info.Set<Vector4Int>(obj, new_value);
                    }
                }
                else
                {
                    //auto dt_enum = Enum::GetEnumByName("EDataType");
                    //const String &dt_name = dt_enum->GetNameByEnum(prop_info.DataType());
                    ImGui::Text("Unsupported Type: %s", prop_info.TypeName().c_str());
                }
            }
            else
                ImGui::Text("ConstValue %s", prop_info.Name().c_str());
            ImGui::PopID();
        }

        static ProfileWindow *s_prifile_wd = nullptr;
        EditorLayer::EditorLayer() : EditorLayer("EditorLayer")
        {

        }

        EditorLayer::EditorLayer(const String &name) : Layer(name)
        {
            s_prifile_wd = new ProfileWindow();
            s_prifile_wd->_content_size = Vector2f(800.0f, 600.0f);
        }

        EditorLayer::~EditorLayer()
        {
            DESTORY_PTR(s_prifile_wd);
        }
        UI::Text *g_text = nullptr;
        static void FindFirstText(UI::Widget *w)
        {
            for (auto c: *w)
            {
                if (c->GetType() == UI::Text::StaticType())
                {
                    g_text = dynamic_cast<UI::Text *>(c.get());
                    break;
                }
            }
        }

        RenderView *render_view;
        void EditorLayer::OnAttach()
        {
            auto r = Render::RenderPipeline::Get().GetRenderer();
            r->AddFeature(&_pick);
            r->SetShadingMode(EShadingMode::kLit);


            DockManager::Init();

            _main_widget = MakeRef<UI::Widget>();

            auto p = Application::Get().GetUseHomePath() + L"OneDrive/AiluEngine/Editor/Res/UI/main_widget.json";
            JsonArchive ar;
            ar.Load(p);
            ar >> *_main_widget;
            FindFirstText(_main_widget.get());
            dynamic_cast<EditorApp &>(Application::Get())._on_file_changed += [&](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                if (PathUtils::GetFileName(cur_path) == L"main_widget")
                {
                    LOG_INFO(L"Reload widget {}", cur_path);
                    JsonArchive ar;
                    ar.Load(cur_path);
                    UI::UIManager::Get()->UnRegisterWidget(_main_widget.get());
                    _main_widget.reset();
                    _main_widget = MakeRef<UI::Widget>();
                    _main_widget->BindOutput(RenderTexture::s_backbuffer, nullptr);
                    ar >> *_main_widget;
                    UI::UIManager::Get()->RegisterWidget(_main_widget);
                    FindFirstText(_main_widget.get());
                }
            };
        }

        void EditorLayer::OnDetach()
        {
            DockManager::Shutdown();
            JsonArchive ar;
            ar << *_main_widget;
            auto p = Application::Get().GetUseHomePath() + L"OneDrive/AiluEngine/Editor/Res/UI/main_widget.json";
            LOG_INFO(L"Save widget to ", p);
            ar.Save(p);
        }
        static CCDSolver ccd_solver;
        //static CCDSolver ccd_solver1;
        static CCDSolver fabrik_solver;
        void EditorLayer::OnEvent(Event &e)
        {
            if (e.GetEventType() == EEventType::kKeyPressed)
            {
                auto &key_e = static_cast<KeyPressedEvent &>(e);
                if (key_e.GetKeyCode() == EKey::kF11)
                {
                    LOG_INFO("Capture frame...");
                    g_pGfxContext->TakeCapture();
                }
                if (key_e.GetKeyCode() == EKey::kS)
                {
                    if (Input::IsKeyPressed(EKey::kCONTROL))
                    {
                        LOG_INFO("Save assets...");
                        g_pThreadTool->Enqueue([]()
                                               {
                                                   f32 asset_count = 1.0f;
                                                   for(auto it = g_pResourceMgr->Begin(); it != g_pResourceMgr->End(); it++)
                                                   {
                                                       g_pResourceMgr->SaveAsset(it->second.get());
                                                       //ImGuiWidget::DisplayProgressBar("SaveAsset...",asset_count / (f32)g_pResourceMgr->AssetNum());
                                                       asset_count += 1.0;
                                                   } });
                    }
                }
                if (key_e.GetKeyCode() == EKey::kZ)
                {
                    if (Input::IsKeyPressed(EKey::kCONTROL))
                    {
                        if (Input::IsKeyPressed(EKey::kSHIFT))
                        {
                            g_pCommandMgr->Redo();
                        }
                        else
                        {
                            g_pCommandMgr->Undo();
                        }
                    }
                }
                if (key_e.GetKeyCode() == EKey::kD)
                {
                    if (Input::IsKeyPressed(EKey::kCONTROL))
                    {
                        List<ECS::Entity> new_entities;
                        for (auto e: Selection::SelectedEntities())
                        {
                            new_entities.push_back(g_pSceneMgr->ActiveScene()->DuplicateEntity(e));
                        }
                        Selection::RemoveSlection();
                        for (auto &e: new_entities)
                            Selection::AddSelection(e);
                    }
                }
                if (key_e.GetKeyCode() == EKey::kF)
                {
                    static u16 s_count = 1;
                    Solver::s_is_apply_constraint = s_count % 2;
                    ccd_solver.IsApplyConstraint(s_count % 2);
                    fabrik_solver.IsApplyConstraint(s_count % 2);
                    ++s_count;
                }
                if (key_e.GetKeyCode() == EKey::kQ)
                    _is_transform_gizmo_world = !_is_transform_gizmo_world;
                else if (key_e.GetKeyCode() == EKey::kW)
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::TRANSLATE;
                else if (key_e.GetKeyCode() == EKey::kE)
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::ROTATE;
                else if (key_e.GetKeyCode() == EKey::kR)
                    _transform_gizmo_type = (i16) ImGuizmo::OPERATION::SCALE;
                else
                {
                };
            }
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                MouseButtonPressedEvent &event = static_cast<MouseButtonPressedEvent &>(e);
                if (event.GetButton() == EKey::kLBUTTON)
                {
                    auto m_pos = Input::GetMousePos();
                    //if (_p_scene_view->Hover(m_pos) && _p_scene_view->Focus())
                    //{
                    //    m_pos = m_pos - _p_scene_view->ContentPosition();
                    //    ECS::Entity closest_entity = _pick.GetPickID((u16)m_pos.x, (u16)m_pos.y);
                    //    LOG_INFO("Pick entity: {}", closest_entity);
                    //    if (Input::IsKeyPressed(EKey::kSHIFT))
                    //        Selection::AddSelection(closest_entity);
                    //    else
                    //        Selection::AddAndRemovePreSelection(closest_entity);
                    //    if (closest_entity == ECS::kInvalidEntity)
                    //        Selection::RemoveSlection();
                    //}
                }
            }
            else if (e.GetEventType() == EEventType::kMouseScroll)
            {
            }
        }
        std::once_flag flag;
        void EditorLayer::OnUpdate(f32 dt)
        {
            DockManager::Get().Update(dt);
            //Gizmo::DrawLine(Vector2f::kZero, Vector2f{200, 200}, Colors::kRed);
            //LOG_INFO("--------------------------------------");
            //LOG_INFO("EditorLayer::OnUpdate: mpos({})", Input::GetMousePos().ToString());
            ccd_solver.Resize(3);
            ccd_solver[0]._position = Vector3f(0, 9, 0);
            ccd_solver[1]._position = Vector3f(0, -3, 0);
            ccd_solver[2]._position = Vector3f(0, -3, 0);
            //ccd_solver[3]._position = Vector3f(0, 0, 1);
            fabrik_solver.Resize(3);
            fabrik_solver[0]._position = Vector3f(0, 9, 0);
            fabrik_solver[1]._position = Vector3f(0, -3, 0);
            fabrik_solver[2]._position = Vector3f(0, -3, 0);
            //fabrik_solver[3]._position = Vector3f(0, 0, 1);


            std::call_once(flag, []()
                           {
                        //for (u16 i = 0; i < ccd_solver.Size(); i++)
            {
                //ccd_solver.AddConstraint(0, new HingeConstraint(Vector3f::kRight));
                //ccd_solver.AddConstraint(1, new HingeConstraint(Vector3f::kRight));
                //ccd_solver.AddConstraint(2, new HingeConstraint(Vector3f::kRight));
                //ccd_solver.AddConstraint(0, new BallSocketConstraint(90.f));
                //ccd_solver.AddConstraint(1, new BallSocketConstraint(-160.f, 0.f));
                //ccd_solver.AddConstraint(2, new BallSocketConstraint(45.f));
                //fabrik_solver.AddConstraint(0, new HingeConstraint(Vector3f::kRight));
                //fabrik_solver.AddConstraint(1, new HingeConstraint(Vector3f::kRight));
                //fabrik_solver.AddConstraint(2, new HingeConstraint(Vector3f::kRight));
                //fabrik_solver.AddConstraint(0, new BallSocketConstraint(90.f));
                //fabrik_solver.AddConstraint(1, new BallSocketConstraint(-160.f,0.f));
                //fabrik_solver.AddConstraint(2, new BallSocketConstraint(45.f));

            } });


            //if (ccd_solver.Size() > 0)
            //{

            //    Gizmo::DrawLine(ccd_solver.GetGlobalTransform(0)._position, ccd_solver.GetGlobalTransform(1)._position, Colors::kRed);
            //    Gizmo::DrawLine(ccd_solver.GetGlobalTransform(1)._position, ccd_solver.GetGlobalTransform(2)._position, Colors::kBlue);
            //    //Gizmo::DrawLine(ccd_solver.GetGlobalTransform(2)._position, ccd_solver.GetGlobalTransform(3)._position, Colors::kWhite);

            //    Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(0)._position, fabrik_solver.GetGlobalTransform(1)._position, Colors::kYellow);
            //    Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(1)._position, fabrik_solver.GetGlobalTransform(2)._position, Colors::kGreen);
            //    //Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(2)._position, fabrik_solver.GetGlobalTransform(3)._position, Colors::kCyan);
            //}
            //            Bezier<Vector3f> curve;
            //            curve.P1 = Vector3f(-5, 0, 0);
            //            curve.P2 = Vector3f(5, 0, 0);
            //            curve.C1 = Vector3f(-2, 1, 0);
            //            curve.C2 = Vector3f(2, 1, 0);
            //            Gizmo::DrawLine(curve.P1,curve.C1,Colors::kBlue);
            //            Gizmo::DrawLine(curve.P2,curve.C2,Colors::kBlue);
            //            for (int i = 0; i < 199; ++ i)
            //            {
            //                float t0 = (float)i / 199.0f;
            //                float t1 = (float)(i + 1) / 199.0f;
            //                Vector3f thisPoint = Bezier<Vector3f>::Interpolate(curve, t0);
            //                Vector3f nextPoint = Bezier<Vector3f>::Interpolate(curve, t1);
            //                Gizmo::DrawLine(thisPoint, nextPoint, Colors::kCyan);
            //            }
        }

        static bool show = false;
        static bool s_show_plot_demo = false;
        static bool s_show_asset_table = false;
        static bool s_show_rt = false;
        static bool s_show_renderview = true;
        static bool s_show_threadpool_view = false;
        static bool s_show_imguinode = false;

        static void ShowThreadPoolView(bool *is_show)
        {
            static Vector<List<std::tuple<String, f32, f32>>> s_foucs_records;
            static bool s_foucs_record = false;
            ImGui::Begin("ThreadPoolView", is_show);
            if (ImGui::Button("Capture"))
            {
                s_foucs_records = g_pThreadTool->TaskTimeRecordView();
                s_foucs_record = !s_foucs_record;
            }
            for (u16 i = 0; i < g_pThreadTool->ThreadNum(); i++)
            {
                ImGui::Text("Thread %d", i);
                ImGui::Indent();
                ImGui::Text("ThreadStatus: %s", Core::EThreadStatus::ToString(g_pThreadTool->Status(i)));
                const auto &records = s_foucs_record ? s_foucs_records[i] : g_pThreadTool->TaskTimeRecord(i);
                if (records.size() > 0)
                {
                    u16 record_index = 0;
                    for (auto &record: records)
                    {
                        auto &[name, start, duration] = record;
                        ImGui::Text("%s %.2fms %.2fms; ", name.c_str(), start, start + duration);
                        if (record_index++ != records.size() - 1)
                            ImGui::SameLine();
                    }
                }
                ImGui::Unindent();
            }
            ImGui::End();
        }

        void EditorLayer::OnImguiRender()
        {
            static bool s_show_undo_view = false;
            //ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
            //if (ImGui::BeginMainMenuBar())
            //{
            //    if (ImGui::BeginMenu("File"))
            //    {
            //        ImGui::EndMenu();
            //    }
            //    if (ImGui::BeginMenu("Edit"))
            //    {
            //        if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
            //        if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}// Disabled item
            //        ImGui::Separator();
            //        if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            //        if (ImGui::MenuItem("Copy", "CTRL+C")) {}
            //        if (ImGui::MenuItem("Paste", "CTRL+V")) {}
            //        ImGui::EndMenu();
            //    }
            //    if (ImGui::BeginMenu("Window"))
            //    {
            //        if (ImGui::MenuItem("BlendSpace"))
            //        {
            //            //g_blend_space_editor->Open(g_blend_space_editor->Handle());
            //        }
            //        ImGui::EndMenu();
            //    }
            //    ImGui::EndMainMenuBar();
            //}

            /*
            ImGui::SetNextWindowSizeConstraints(ImVec2(100, 0), ImVec2(FLT_MAX, ImGui::GetTextLineHeight()));
            ImGui::Begin("ToolBar", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            Application* app = &Application::Get();
            float window_width = ImGui::GetContentRegionAvail().x;// 获取当前窗口的可用宽度
            float button_width = 100.0f;                          // 假设每个按钮宽度为100，实际可用ImGui::CalcTextSize计算

            if (app->_is_playing_mode || app->_is_simulate_mode)
            {
                if (app->_is_playing_mode)
                {
                    float total_width = button_width;                         // 只有一个"Stop"按钮
                    ImGui::SetCursorPosX((window_width - total_width) * 0.5f);// 设置水平居中

                    if (ImGui::Button("Stop", ImVec2(button_width, 0)))
                    {
                        g_pSceneMgr->ExitPlayMode();
                    }
                }
                if (app->_is_simulate_mode)
                {
                    float total_width = button_width;                         // 只有一个"Stop"按钮
                    ImGui::SetCursorPosX((window_width - total_width) * 0.5f);// 设置水平居中

                    if (ImGui::Button("Stop", ImVec2(button_width, 0)))
                    {
                        g_pSceneMgr->ExitSimulateMode();
                    }
                }
            }
            else
            {
                // 假设"Play"和"Simulate"按钮的总宽度
                float total_width = button_width * 2 + ImGui::GetStyle().ItemSpacing.x;// 两个按钮 + 间距
                ImGui::SetCursorPosX((window_width - total_width) * 0.5f);             // 设置水平居中

                if (ImGui::Button("Play", ImVec2(button_width, 0)))
                {
                    g_pSceneMgr->EnterPlayMode();
                }
                ImGui::SameLine();
                if (ImGui::Button("Simulate", ImVec2(button_width, 0)))
                {
                    g_pSceneMgr->EnterSimulateMode();
                }
            }
            ImGui::End();
            */
            ImGui::Begin("Common");// Create a window called "Hello, world!" and append into it.
            ImGui::Text("FrameRate: %.2f", RenderingStates::s_frame_rate);
            ImGui::Text("FrameTime: %.2f ms", RenderingStates::s_frame_time);
            ImGui::Text("GpuLatency: %.2f ms", RenderingStates::s_gpu_latency);
            ImGui::Text("Draw Call: %d", RenderingStates::s_draw_call);
            ImGui::Text("Dispatch Call: %d", RenderingStates::s_dispatch_call);
            ImGui::Text("VertCount: %d", RenderingStates::s_vertex_num);
            ImGui::Text("TriCount: %d", RenderingStates::s_triangle_num);
            if (ImGui::CollapsingHeader("Features"))
            {
                for (auto feature: Render::RenderPipeline::Get().GetRenderer()->GetFeatures())
                {
                    bool active = feature->IsActive();
                    ImGui::Checkbox(feature->Name().c_str(), &active);
                    feature->SetActive(active);
                    auto *cur_type = feature->GetType();
                    ImGui::Text("Type: %s", cur_type->Name().c_str());
                    ImGui::Text("Members: ");
                    //if (cur_type->BaseType() && cur_type->BaseType() != cur_type)//除去Object
                    {
                        for (auto &prop: cur_type->GetProperties())
                        {
                            DrawMemberProperty(prop, *feature);
                        }
                    }
                }
            }

            static bool s_show_shadding_mode = false;
            if (ImGui::CollapsingHeader("ShadingMode"))
            {
                static u16 s_selected_chechbox = 0;
                bool checkbox1 = (s_selected_chechbox == 0);
                if (ImGui::Checkbox("Lit", &checkbox1))
                {
                    if (checkbox1)
                    {
                        Render::RenderPipeline::Get().GetRenderer()->SetShadingMode(EShadingMode::kLit);
                        s_selected_chechbox = 0;
                    }
                }
                bool checkbox2 = (s_selected_chechbox == 1);
                if (ImGui::Checkbox("Wireframe", &checkbox2))
                {
                    if (checkbox2)
                    {
                        Render::RenderPipeline::Get().GetRenderer()->SetShadingMode(EShadingMode::kWireframe);
                        s_selected_chechbox = 1;
                    }
                }
                bool checkbox3 = (s_selected_chechbox == 2);
                if (ImGui::Checkbox("LitWireframe", &checkbox3))
                {
                    if (checkbox3)
                    {
                        Render::RenderPipeline::Get().GetRenderer()->SetShadingMode(EShadingMode::kLitWireframe);
                        s_selected_chechbox = 2;
                    }
                }
            }
            // static bool s_show_pass = false;
            // if (ImGui::CollapsingHeader("Features"))
            // {
            //     for (auto feature: Render::RenderPipeline::Get().GetRenderer()->GetFeatures())
            //     {
            //         bool active = feature->IsActive();
            //         ImGui::Checkbox(feature->Name().c_str(), &active);
            //         feature->SetActive(active);
            //     }
            // }
            static bool s_show_time_info = false;
            if (ImGui::CollapsingHeader("Performace Statics"))
            {
                ImGui::Text("GPU Time:");
                for (const auto &it: Profiler::Get().GetPassGPUTime())
                {
                    auto &[name, time] = it;
                    ImGui::Text("   %s,%.2f ms", name.c_str(), time);
                }
            }
            ImGui::SliderFloat("Gizmo Alpha:", &Gizmo::s_color.a, 0.01f, 1.0f, "%.2f");
            ImGui::SliderFloat("Game Time Scale:", &TimeMgr::s_time_scale, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("ShadowDistance m", &QuailtySetting::s_main_light_shaodw_distance, 0.f, 100.0f, "%.2f");

            //g_pRenderer->_shadow_distance = shadow_dis_m * 100.0f;
            static bool s_show_anim_clip = false;
            ImGui::Checkbox("Expand", &show);
            ImGui::Checkbox("ShowPlotDemo", &s_show_plot_demo);
            ImGui::Checkbox("ShowAssetTable", &s_show_asset_table);
            ImGui::Checkbox("ShowRT", &s_show_rt);
            ImGui::Checkbox("ShowUndoView", &s_show_undo_view);
            ImGui::Checkbox("ShowAnimClip", &s_show_anim_clip);
            ImGui::Checkbox("ShowThreadPoolView", &s_show_threadpool_view);
            ImGui::Checkbox("ShowNode", &s_show_imguinode);
            ImGui::Checkbox("UseRenderGraph", &RenderPipeline::Get().GetRenderer()->_is_use_render_graph);

            // for (auto &info: g_pResourceMgr->GetImportInfos())
            // {
            //     float x = g_pTimeMgr->GetScaledWorldTime(0.25f);
            //     ImGui::Text("%s", info._msg.c_str());
            //     ImGui::SameLine();
            //     ImGui::ProgressBar(x - static_cast<int>(x), ImVec2(0.f, 0.f));
            // }
            ECS::Entity e = Selection::FirstEntity();
            auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
            if (e != ECS::kInvalidEntity)
            {
                if (r.HasComponent<ECS::CCamera>(e))
                {
                    auto &cam = r.GetComponent<ECS::CCamera>(e)->_camera;
                    cam.SetPixelSize(300, (u16) (300.0f / cam.Aspect()));
                    //static_cast<RenderView *>(_p_preview_cam_view)->SetSource(Render::RenderPipeline::Get().GetTarget(1));
                    //_p_preview_cam_view->Open(_p_preview_cam_view->Handle());
                    Camera::sSelected = &cam;
                }
                else
                {
                    Camera::sSelected = nullptr;
                    //_p_preview_cam_view->Close(_p_preview_cam_view->Handle());
                }
                if (auto sk_comp = r.GetComponent<ECS::CSkeletonMesh>(e); sk_comp != nullptr)
                {
                    //g_blend_space_editor->SetTarget(&sk_comp->_blend_space);
                }
                if (auto sk_comp = r.GetComponent<ECS::TransformComponent>(e); sk_comp != nullptr)
                {
                    ImGui::DragFloat3("Position", sk_comp->_transform._position.data);
                    Vector3f euler = Quaternion::EulerAngles(sk_comp->_transform._rotation);
                    ImGui::DragFloat3("Rotation", euler.Data());
                    sk_comp->_transform._rotation = Quaternion::EulerAngles(euler);
                    ImGui::DragFloat3("Scale", sk_comp->_transform._scale.data);
                }
            }
            static bool s_draw_scene_bvh = false;
            static bool s_draw_mesh_bvh = false;
            //bvh debug
            {
                ImGui::Checkbox("ShowSceneBVH", &s_draw_scene_bvh);
                if (s_draw_scene_bvh)
                {
                    for (const auto &n: g_pSceneMgr->ActiveScene()->GetBVHNodes())
                        Render::Gizmo::DrawAABB(n._aabb);
                }
                if (e != ECS::kInvalidEntity)
                {
                    ImGui::Checkbox("DrawMeshBVH", &s_draw_mesh_bvh);
                    static auto draw_tri = [](float3 v0, float3 v1, float3 v2, Color c = Colors::kGreen)
                    {
                        Gizmo::DrawLine(v0, v1, c);
                        Gizmo::DrawLine(v1, v2, c);
                        Gizmo::DrawLine(v2, v0, c);
                    };
                    static auto scale_aabb = [](const AABB &b, f32 scale) -> auto
                    {
                        AABB result = b;
                        Vector3f dmin = b._min - b.Center();
                        Vector3f dmax = b._max - b.Center();
                        dmin *= scale;
                        dmax *= scale;
                        result._min = b.Center() + dmin;
                        result._max = b.Center() + dmax;
                        return result;
                    };
                    if (auto sk_comp = r.GetComponent<ECS::StaticMeshComponent>(e); sk_comp != nullptr)
                    {
                        if (s_draw_mesh_bvh)
                        {
                            const auto &nodes = sk_comp->_p_mesh->GetBVHNodes();
                            static i32 draw_idx = -1;
                            ImGui::SliderInt("MeshBVH Index", &draw_idx, -1, nodes.size() - 1);
                            if (draw_idx == -1)
                            {
                                const auto &tri_data = sk_comp->_p_mesh->GetTriangleData();
                                Matrix4x4f scale = MatrixScale(0.9f, 0.9f, 0.9);
                                for (const auto &n: nodes)
                                {
                                    if (!n.IsLeaf())
                                        continue;
                                    for (u32 i = n._child_index_or_first; i < n._child_index_or_first + n._count_or_flag; i++)
                                    {
                                        Vector3f v0 = TransformCoord(scale, tri_data[i].v0);
                                        Vector3f v1 = TransformCoord(scale, tri_data[i].v1);
                                        Vector3f v2 = TransformCoord(scale, tri_data[i].v2);
                                        draw_tri(v0, v1, v2, Colors::kCyan);
                                    }
                                }
                            }
                            else
                            {
                                const auto &n = nodes[draw_idx];
                                Render::Gizmo::DrawAABB(n._aabb);
                                if (n.IsLeaf())
                                {
                                    Matrix4x4f scale = MatrixScale(0.9f, 0.9f, 0.9);
                                    const auto &tri_data = sk_comp->_p_mesh->GetTriangleData();
                                    for (u32 i = n._child_index_or_first; i < n._child_index_or_first + n._count_or_flag; i++)
                                    {
                                        Vector3f v0 = TransformCoord(scale, tri_data[i].v0);
                                        Vector3f v1 = TransformCoord(scale, tri_data[i].v1);
                                        Vector3f v2 = TransformCoord(scale, tri_data[i].v2);
                                        draw_tri(v0, v1, v2, Colors::kCyan);
                                    }
                                }
                                else
                                {
                                    Render::Gizmo::DrawAABB(scale_aabb(nodes[n._child_index_or_first]._aabb, 0.9f), Colors::kGreen);
                                    Render::Gizmo::DrawAABB(scale_aabb(nodes[-n._count_or_flag]._aabb, 0.9f), Colors::kGreen);
                                }
                            }
                        }
                        else//triangle aabb
                        {
                            static i32 draw_idx = -1;
                            const auto &tri = sk_comp->_p_mesh->GetTriangleData();
                            const auto &tri_bounds = sk_comp->_p_mesh->GetTriangleBounds();
                            ImGui::SliderInt("Tri Index", &draw_idx, -1, tri.size() - 1);
                            if (draw_idx != -1)
                            {
                                Render::Gizmo::DrawAABB(tri_bounds[draw_idx], Colors::kGreen);
                                draw_tri(tri[draw_idx].v0, tri[draw_idx].v1, tri[draw_idx].v2, Colors::kCyan);
                            }
                        }
                    }
                }
            }
            if (ImGui::Button("ProfileWindow"))
                s_prifile_wd->_is_show = !s_prifile_wd->_is_show;
            static Vector4f color;
            ImGui::ColorEdit4("Color", color.Data());
            ImGui::End();
            if (show)
                ImGui::ShowDemoWindow(&show);
            if (s_show_plot_demo)
                ImPlot::ShowDemoWindow((&s_show_plot_demo));
            static RenderTexture *s_last_scene_rt = nullptr;
            auto scene_rt = Render::RenderPipeline::Get().GetTarget(0);
            if (s_last_scene_rt != scene_rt)
            {
                //LOG_INFO("Scene RT Changed");
            }
            s_last_scene_rt = scene_rt;

            if (s_show_undo_view)
            {
                ImGui::Begin("UndoView", &s_show_undo_view);
                for (auto cmd: g_pCommandMgr->UndoViews())
                {
                    ImGui::Text("%s", cmd->ToString().c_str());
                }
                ImGui::End();
            }
            s_prifile_wd->Show();
        }

        void EditorLayer::Begin()
        {
        }

        void EditorLayer::End()
        {
        }
        void EditorLayer::ProcessTransformGizmo()
        {
            //auto &selected_entities = Selection::SelectedEntities();
            //if (_transform_gizmo_type == -1 || selected_entities.empty())
            //    return;
            ////snap
            //f32 snap_value = 0.5f;//50cm
            //if (_transform_gizmo_type == (i16) ImGuizmo::OPERATION::ROTATE)
            //{
            //    snap_value = 22.5f;//degree
            //}
            //else if (_transform_gizmo_type == (i16) ImGuizmo::OPERATION::SCALE)
            //{
            //    snap_value = 0.25f;
            //}
            //else
            //{
            //}
            //Vector3f snap_values = Vector3f(snap_value);
            //ImGuizmo::SetOrthographic(false);
            //ImGuizmo::SetDrawlist();
            //ImGuizmo::SetRect(_scene_vp_rect.x, _scene_vp_rect.y, _scene_vp_rect.z, _scene_vp_rect.w);
            //const auto &view = Camera::sCurrent->GetView();
            //auto proj = MatrixReverseZ(Camera::sCurrent->GetProjNoJitter());
            //_is_transform_gizmo_snap = Input::IsKeyPressed(EKey::kCONTROL);
            //bool is_pivot_point_center = true;
            //bool is_single_mode = selected_entities.size() == 1;
            //Vector3f pivot_point;
            //if (is_pivot_point_center)
            //{
            //    auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
            //    Matrix4x4f world_mat;
            //    if (is_single_mode)
            //        world_mat = r.GetComponent<ECS::TransformComponent>(selected_entities.front())->_transform._world_matrix;
            //    else
            //        _is_transform_gizmo_world = true;
            //    ImGuizmo::Manipulate(view, proj, (ImGuizmo::OPERATION) _transform_gizmo_type, _is_transform_gizmo_world ? ImGuizmo::WORLD : ImGuizmo::LOCAL, world_mat, nullptr,
            //                         _is_transform_gizmo_snap ? snap_values.data : nullptr);
            //    static bool s_can_duplicate = true;
            //    if (ImGuizmo::IsUsing())
            //    {
            //        if (!_is_begin_gizmo_transform)
            //        {
            //            _is_begin_gizmo_transform = true;
            //            _is_end_gizmo_transform = false;
            //            for (auto e: selected_entities)
            //            {
            //                _old_trans.push_back(r.GetComponent<ECS::TransformComponent>(e)->_transform);
            //            }
            //            if (!is_single_mode)
            //            {
            //                for (auto e: selected_entities)
            //                {
            //                    pivot_point += r.GetComponent<ECS::TransformComponent>(e)->_transform._position;
            //                }
            //                pivot_point /= (f32) selected_entities.size();
            //                world_mat = MatrixTranslation(pivot_point);
            //            }
            //        }
            //        Vector3f new_pos;
            //        Vector3f new_scale;
            //        Quaternion new_rot;
            //        DecomposeMatrix(world_mat, new_pos, new_rot, new_scale);
            //        static Vector3f s_pre_tick_new_scale = new_scale;
            //        if (is_single_mode)
            //        {
            //            auto &t = r.GetComponent<ECS::TransformComponent>(selected_entities.front())->_transform;
            //            t._position = new_pos;
            //            t._rotation = new_rot;
            //            t._scale = new_scale;
            //        }
            //        else
            //        {
            //            u16 index = 0;
            //            for (auto e: selected_entities)
            //            {
            //                auto &t = r.GetComponent<ECS::TransformComponent>(e)->_transform;
            //                Vector3f rela_pos = t._position - pivot_point;
            //                t._position = rela_pos + new_pos;
            //                //rela_pos = new_rot * rela_pos;
            //                //Vector3f scaled_pos = rela_pos * (new_scale / s_pre_tick_new_scale);
            //                //Vector3f pivot_offset = new_pos - pivot_point;
            //                //t._position = pivot_point + scaled_pos + pivot_offset;
            //                //TODO
            //                //t._scale = new_scale * _old_trans[index]._scale;
            //                //t._rotation = new_rot * _old_trans[index++]._rotation;
            //            }
            //        }
            //        s_pre_tick_new_scale = new_scale;
            //        /*
            //        区分左侧和右侧的 Alt 键需要结合使用扫描码。在处理低级键盘输入时，可以通过 GetKeyState 或 GetAsyncKeyState 等函数来检测具体的按键位置。
            //        对于左侧 Alt 键，它的扫描码为：0x38(扫描码)
            //        右侧 Alt 键(也称 AltGr)：0xE038(扩展扫描码)
            //        */
            //        if (s_can_duplicate && Input::IsKeyPressed(EKey::kALT) && _transform_gizmo_type == (i16) ImGuizmo::OPERATION::TRANSLATE)
            //        {
            //            s_can_duplicate = false;
            //            List<ECS::Entity> new_entities;
            //            for (auto e: selected_entities)
            //            {
            //                new_entities.push_back(g_pSceneMgr->ActiveScene()->DuplicateEntity(e));
            //            }
            //            Selection::RemoveSlection();
            //            for (auto &e: new_entities)
            //                Selection::AddSelection(e);
            //        }
            //    }
            //    else
            //    {
            //        if (_is_begin_gizmo_transform)
            //        {
            //            s_can_duplicate = true;
            //            _is_end_gizmo_transform = true;
            //            _is_begin_gizmo_transform = false;
            //            if (is_single_mode)
            //            {
            //                auto e = selected_entities.front();
            //                auto t = r.GetComponent<ECS::TransformComponent>(e);
            //                std::unique_ptr<ICommand> moveCommand1 = std::make_unique<TransformCommand>(r.GetComponent<ECS::TagComponent>(e)->_name, t, _old_trans.front());
            //                g_pCommandMgr->ExecuteCommand(std::move(moveCommand1));
            //                _old_trans.clear();
            //            }
            //            else
            //            {
            //                Vector<String> obj_names;
            //                Vector<ECS::TransformComponent *> comps;
            //                u16 index = 0u;
            //                for (auto e: selected_entities)
            //                {
            //                    obj_names.emplace_back(r.GetComponent<ECS::TagComponent>(e)->_name);
            //                    comps.emplace_back(r.GetComponent<ECS::TransformComponent>(e));
            //                }
            //                std::unique_ptr<ICommand> moveCommand1 = std::make_unique<TransformCommand>(std::move(obj_names), std::move(comps), std::move(_old_trans));
            //                g_pCommandMgr->ExecuteCommand(std::move(moveCommand1));
            //            }
            //        }
            //    }
            //    Selection::Active(!ImGuizmo::IsOver());
            //}
        }
    }// namespace Editor
}// namespace Ailu
