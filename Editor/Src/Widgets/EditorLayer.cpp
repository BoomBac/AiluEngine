#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"
#include "Ext/imnodes/imnodes.h"
#include "Ext/implot/implot.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/TimeMgr.h"
#include "Render/Gizmo.h"
#include "Render/Pass/PostprocessPass.h"
#include "Render/Pass/VolumetricClouds.h"
#include "Render/RenderingData.h"
#include "Render/TextRenderer.h"
#include <Framework/Common/Application.h>
#include <Objects/Type.h>

#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"

#include "Framework/Common/Profiler.h"
#include "Render/RenderPipeline.h"

#include "Animation/Curve.hpp"
#include "Animation/Solver.h"
#include "Common/Undo.h"
#include "Framework/Events/KeyEvent.h"
#include "Render/CommonRenderPipeline.h"
#include "Widgets/AnimClipEditor.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/AssetTable.h"
#include "Widgets/BlendSpaceEditor.h"
#include "Widgets/CommonTextureWidget.h"
#include "Widgets/EnvironmentSetting.h"
#include "Widgets/ObjectDetail.h"
#include "Widgets/OutputLog.h"
#include "Widgets/PlaceActors.h"
#include "Widgets/RenderView.h"
#include "Widgets/WorldOutline.h"
#include "Widgets/ProfileWindow.h"

namespace Ailu
{
    namespace Editor
    {
        // User callback
        static void mini_map_node_hovering_callback(int node_id, void *user_data)
        {
            ImGui::SetTooltip("This is node %d", node_id);
        }

        static void ShowImNodeTest(bool *is_show)
        {
            const int hardcoded_node_id = 1;
            ImGui::Begin("Node", is_show);
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(ImGuiWidget::kDragAssetAnimClip.c_str());
                if (payload)
                {
                    auto clip = *(Asset **) (payload->Data);
                    auto clip_ref = clip->AsRef<AnimationClip>();
                    LOG_INFO("{}", clip_ref->Name().c_str());
                }
                ImGui::EndDragDropTarget();
            }
            ImNodes::BeginNodeEditor();
            {
                ImNodes::BeginNode(hardcoded_node_id);
                ImNodes::BeginNodeTitleBar();
                ImGui::TextUnformatted("Title");
                ImNodes::EndNodeTitleBar();

                ImGui::Dummy(ImVec2(80.0f, 45.0f));
                const int output_attr_id = 2;
                ImNodes::BeginOutputAttribute(output_attr_id);
                // in between Begin|EndAttribute calls, you can call ImGui
                // UI functions
                ImGui::Text("output pin");
                ImNodes::EndOutputAttribute();

                ImNodes::EndNode();
            }
            ImNodes::MiniMap();
            //ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_TopRight, mini_map_node_hovering_callback);
            ImNodes::EndNodeEditor();
            ImGui::End();
        }
        BlendSpaceEditor *g_blend_space_editor;
        EditorLayer::EditorLayer() : Layer("EditorLayer")
        {
            ImGuiIO &io = ImGui::GetIO();
            (void) io;
            //_font = io.Fonts->AddFontFromFileTTF(ResourceMgr::GetResSysPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
            _font = io.Fonts->AddFontFromFileTTF(ResourceMgr::GetResSysPath("Fonts/Open_Sans/static/OpenSans-Regular.ttf").c_str(), 14.0f);
            io.Fonts->Build();
        }

        EditorLayer::EditorLayer(const String &name) : Layer(name)
        {
        }

        EditorLayer::~EditorLayer()
        {
        }

        void EditorLayer::OnAttach()
        {
            auto tex_detail_view = _widgets.emplace_back(std::move(MakeScope<TextureDetailView>())).get();
            _widgets.emplace_back(std::move(MakeScope<AssetBrowser>(tex_detail_view)));
            _widgets.emplace_back(std::move(MakeScope<AssetTable>()));
            _widgets.emplace_back(std::move(MakeScope<WorldOutline>()));
            _widgets.emplace_back(std::move(MakeScope<ObjectDetail>()));
            _widgets.emplace_back(std::move(MakeScope<PlaceActors>()));
            _widgets.emplace_back(std::move(MakeScope<RenderTextureView>()));
            for (auto appender: g_pLogMgr->GetAppenders())
            {
                auto imgui_appender = dynamic_cast<ImGuiLogAppender *>(appender);
                if (imgui_appender)
                {
                    _widgets.emplace_back(std::move(MakeScope<OutputLog>(imgui_appender)));
                }
            }

            _widgets.emplace_back(std::move(MakeScope<SceneView>()));
            _p_scene_view = _widgets.back().get();
            _widgets.emplace_back(std::move(MakeScope<RenderView>()));
            _p_preview_cam_view = _widgets.back().get();
            _widgets.emplace_back(std::move(MakeScope<EnvironmentSetting>()));
            _p_env_setting = _widgets.back().get();
            _widgets.emplace_back(std::move(MakeScope<BlendSpaceEditor>()));
            g_blend_space_editor = static_cast<BlendSpaceEditor *>(_widgets.back().get());
            _widgets.emplace_back(std::move(MakeScope<ProfileWindow>()));
            _p_profiler_window = _widgets.back().get();
            for (auto &widget: _widgets)
            {
                widget->Open(ImGuiWidget::GetGlobalWidgetHandle());
            }
            //默认不显示纹理细节窗口
            tex_detail_view->Close(tex_detail_view->Handle());
            g_blend_space_editor->Close(g_blend_space_editor->Handle());
            _p_preview_cam_view->Close(_p_preview_cam_view->Handle());
            _p_profiler_window->Close(_p_profiler_window->Handle());
            ImGuiWidget::SetFocus("AssetBrowser");
            auto r = static_cast<CommonRenderPipeline *>(g_pGfxContext->GetPipeline())->GetRenderer();
            r->AddFeature(&_pick);
            r->SetShadingMode(EShadingMode::kLit);
        }

        void EditorLayer::OnDetach()
        {
        }
        static CCDSolver ccd_solver;
        //static CCDSolver ccd_solver1;
        static CCDSolver fabrik_solver;
        void EditorLayer::OnEvent(Event &e)
        {
            for (auto &w: _widgets)
                w->OnEvent(e);
            if (e.GetEventType() == EEventType::kKeyPressed)
            {
                auto &key_e = static_cast<KeyPressedEvent &>(e);
                if (key_e.GetKeyCode() == AL_KEY_F11)
                {
                    LOG_INFO("Capture frame...");
                    g_pGfxContext->TakeCapture();
                }
                if (key_e.GetKeyCode() == AL_KEY_S)
                {
                    if (Input::IsKeyPressed(AL_KEY_CONTROL))
                    {
                        LOG_INFO("Save assets...");
                        g_pThreadTool->Enqueue([]()
                                               {
                                                   f32 asset_count = 1.0f;
                                                   for(auto it = g_pResourceMgr->Begin(); it != g_pResourceMgr->End(); it++)
                                                   {
                                                       g_pResourceMgr->SaveAsset(it->second.get());
                                                       ImGuiWidget::DisplayProgressBar("SaveAsset...",asset_count / (f32)g_pResourceMgr->AssetNum());
                                                       asset_count += 1.0;
                                                   } });
                    }
                }
                if (key_e.GetKeyCode() == AL_KEY_Z)
                {
                    if (Input::IsKeyPressed(AL_KEY_CONTROL))
                    {
                        if (Input::IsKeyPressed(AL_KEY_SHIFT))
                        {
                            g_pCommandMgr->Redo();
                        }
                        else
                        {
                            g_pCommandMgr->Undo();
                        }
                    }
                }
                if (key_e.GetKeyCode() == AL_KEY_D)
                {
                    if (Input::IsKeyPressed(AL_KEY_CONTROL))
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
                if (key_e.GetKeyCode() == AL_KEY_F)
                {
                    static u16 s_count = 1;
                    Solver::s_is_apply_constraint = s_count % 2;
                    ccd_solver.IsApplyConstraint(s_count % 2);
                    fabrik_solver.IsApplyConstraint(s_count % 2);
                    ++s_count;
                }
            }
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                MouseButtonPressedEvent &event = static_cast<MouseButtonPressedEvent &>(e);
                if (event.GetButton() == AL_KEY_LBUTTON)
                {
                    auto m_pos = Input::GetMousePos();
                    if (_p_scene_view->Hover(m_pos) && _p_scene_view->Focus())
                    {
                        m_pos = m_pos - _p_scene_view->ContentPosition();
                        ECS::Entity closest_entity = _pick.GetPickID(m_pos.x, m_pos.y);
                        LOG_INFO("Pick entity: {}", closest_entity);
                        if (Input::IsKeyPressed(AL_KEY_SHIFT))
                            Selection::AddSelection(closest_entity);
                        else
                            Selection::AddAndRemovePreSelection(closest_entity);
                        if (closest_entity == ECS::kInvalidEntity)
                            Selection::RemoveSlection();
                    }
                }
            }
            else if (e.GetEventType() == EEventType::kMouseScrolled)
            {
                MouseScrollEvent event = static_cast<MouseScrollEvent &>(e);
                f32 zoom = ImNodes::GetZoomFactor();
                if (event.GetOffsetY() > 0)
                    zoom += 0.01f;
                else
                    zoom -= 0.01f;
                ImNodes::SetZoomFactor(zoom);
            }
        }
        std::once_flag flag;
        void EditorLayer::OnUpdate(f32 dt)
        {

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


            if (ccd_solver.Size() > 0)
            {

                Gizmo::DrawLine(ccd_solver.GetGlobalTransform(0)._position, ccd_solver.GetGlobalTransform(1)._position, Colors::kRed);
                Gizmo::DrawLine(ccd_solver.GetGlobalTransform(1)._position, ccd_solver.GetGlobalTransform(2)._position, Colors::kBlue);
                //Gizmo::DrawLine(ccd_solver.GetGlobalTransform(2)._position, ccd_solver.GetGlobalTransform(3)._position, Colors::kWhite);

                Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(0)._position, fabrik_solver.GetGlobalTransform(1)._position, Colors::kYellow);
                Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(1)._position, fabrik_solver.GetGlobalTransform(2)._position, Colors::kGreen);
                //Gizmo::DrawLine(fabrik_solver.GetGlobalTransform(2)._position, fabrik_solver.GetGlobalTransform(3)._position, Colors::kCyan);
            }
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
                ImGui::Text("ThreadStatus: %s", EThreadStatus::ToString(g_pThreadTool->Status(i)));
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
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
                    if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}// Disabled item
                    ImGui::Separator();
                    if (ImGui::MenuItem("Cut", "CTRL+X")) {}
                    if (ImGui::MenuItem("Copy", "CTRL+C")) {}
                    if (ImGui::MenuItem("Paste", "CTRL+V")) {}
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Window"))
                {
                    if (ImGui::MenuItem("BlendSpace"))
                    {
                        g_blend_space_editor->Open(g_blend_space_editor->Handle());
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
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
                for (auto feature: g_pGfxContext->GetPipeline()->GetRenderer()->GetFeatures())
                {
                    bool active = feature->IsActive();
                    ImGui::Checkbox(feature->Name().c_str(), &active);
                    feature->SetActive(active);
                    auto *cur_type = feature->GetType();
                    ImGui::Text("Type: %s", cur_type->Name().c_str());
                    ImGui::Text("Members: ");
                    if (cur_type->BaseType() && cur_type->BaseType() != cur_type)//除去Object
                    {
                        for (auto prop: cur_type->GetProperties())
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
                        g_pGfxContext->GetPipeline()->GetRenderer()->SetShadingMode(EShadingMode::kLit);
                        s_selected_chechbox = 0;
                    }
                }
                bool checkbox2 = (s_selected_chechbox == 1);
                if (ImGui::Checkbox("Wireframe", &checkbox2))
                {
                    if (checkbox2)
                    {
                        g_pGfxContext->GetPipeline()->GetRenderer()->SetShadingMode(EShadingMode::kWireframe);
                        s_selected_chechbox = 1;
                    }
                }
                bool checkbox3 = (s_selected_chechbox == 2);
                if (ImGui::Checkbox("LitWireframe", &checkbox3))
                {
                    if (checkbox3)
                    {
                        g_pGfxContext->GetPipeline()->GetRenderer()->SetShadingMode(EShadingMode::kLitWireframe);
                        s_selected_chechbox = 2;
                    }
                }
            }
            // static bool s_show_pass = false;
            // if (ImGui::CollapsingHeader("Features"))
            // {
            //     for (auto feature: g_pGfxContext->GetPipeline()->GetRenderer()->GetFeatures())
            //     {
            //         bool active = feature->IsActive();
            //         ImGui::Checkbox(feature->Name().c_str(), &active);
            //         feature->SetActive(active);
            //     }
            // }
            static bool s_show_time_info = false;
            if (ImGui::CollapsingHeader("Performace Statics"))
            {
                String space = "";
                // ImGui::Text("CPU Time:");
                // while (!Profiler::Get()._cpu_profiler_queue.empty())
                // {
                //     auto [is_start, profiler_id] = Profiler::Get()._cpu_profiler_queue.front();
                //     Profiler::Get()._cpu_profiler_queue.pop();
                //     if (is_start)
                //     {
                //         space.append("-");
                //         const auto &profiler = Profiler::Get().GetCPUProfileData(profiler_id);
                //         ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
                //     }
                //     else
                //         space = space.substr(0, space.size() - 1);
                // }
                // space = "";
                ImGui::Text("GPU Time:");
                while (!Profiler::Get()._gpu_profiler_queue.empty())
                {
                    const auto& marker = Profiler::Get()._gpu_profiler_queue.front();
                    Profiler::Get()._gpu_profiler_queue.pop();
                    if (marker._is_start)
                    {
                        space.append("-");
                        const auto &profiler = Profiler::Get().GetGpuProfileData(marker._profiler_index);
                        ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
                    }
                    else
                        space = space.substr(0, space.size() - 1);
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
            if (ImGui::Button("Profile"))
                _p_profiler_window->Open(_p_profiler_window->Handle());

            // for (auto &info: g_pResourceMgr->GetImportInfos())
            // {
            //     float x = g_pTimeMgr->GetScaledWorldTime(0.25f);
            //     ImGui::Text("%s", info._msg.c_str());
            //     ImGui::SameLine();
            //     ImGui::ProgressBar(x - static_cast<int>(x), ImVec2(0.f, 0.f));
            // }
            ImGui::End();
            if (show)
                ImGui::ShowDemoWindow(&show);
            if (s_show_plot_demo)
                ImPlot::ShowDemoWindow((&s_show_plot_demo));
            ECS::Entity e = Selection::FirstEntity();
            auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
            if (e != ECS::kInvalidEntity)
            {
                if (r.HasComponent<ECS::CCamera>(e))
                {
                    auto &cam = r.GetComponent<ECS::CCamera>(e)->_camera;
                    cam.SetPixelSize(300, (u16) (300.0f / cam.Aspect()));
                    static_cast<RenderView *>(_p_preview_cam_view)->SetSource(g_pGfxContext->GetPipeline()->GetTarget(1));
                    _p_preview_cam_view->Open(_p_preview_cam_view->Handle());
                    Camera::sSelected = &cam;
                }
                else
                {
                    Camera::sSelected = nullptr;
                    _p_preview_cam_view->Close(_p_preview_cam_view->Handle());
                }
                if (auto sk_comp = r.GetComponent<ECS::CSkeletonMesh>(e); sk_comp != nullptr)
                {
                    g_blend_space_editor->SetTarget(&sk_comp->_blend_space);
                }
            }
            static RenderTexture* s_last_scene_rt = nullptr;
            auto scene_rt = g_pGfxContext->GetPipeline()->GetTarget(0);
            if (s_last_scene_rt != scene_rt)
            {
                //LOG_INFO("Scene RT Changed");
            }
            s_last_scene_rt = scene_rt;
            static_cast<SceneView *>(_p_scene_view)->SetSource(scene_rt);

            for (auto &widget: _widgets)
            {
                widget->Show();
            }
            if (s_show_threadpool_view)
                ShowThreadPoolView(&s_show_threadpool_view);
            ImGuiWidget::ShowProgressBar();
            if (s_show_undo_view)
            {
                ImGui::Begin("UndoView", &s_show_undo_view);
                for (auto cmd: g_pCommandMgr->UndoViews())
                {
                    ImGui::Text("%s", cmd->ToString().c_str());
                }
                ImGui::End();
            }
            static AnimClipEditor anim_editor;
            if (s_show_anim_clip)
            {
                anim_editor.Open(ImGuiWidget::GetGlobalWidgetHandle());
                anim_editor.Show();
            }
            else
            {
                anim_editor.Close(anim_editor.Handle());
            }
            /*
            ImGui::Begin("Solver");

            //            static char buf[256];
            //            static f32 text_scale = 1.0f;
            //            static bool s_show_text_grid = false;
            //            static Color s_tex_color = Colors::kWhite;
            //            ImGui::SliderFloat("TextScale",&text_scale,0.1f,5.0f);
            //            ImGui::Checkbox("TextGrid",&s_show_text_grid);
            //            ImGui::ColorPicker3("TextColor",s_tex_color.data);
            //            auto tr = TextRenderer::Get();
            //            tr->_is_draw_debug_line = s_show_text_grid;
            //            if (ImGui::InputText("TextRenderer Test",buf,256,ImGuiInputTextFlags_EnterReturnsTrue))
            //            {
            //                tr->DrawText(tr->GetDefaultFont(),buf,Vector2f::kZero,Vector2f(text_scale),Vector2f::kOne,s_tex_color);
            //            }
            //            if (ImGui::Button("Clear"))
            //                TextRenderer::Get()->ClearBuffer();
            ImGui::Checkbox("ApplyConstraint", &Solver::s_is_apply_constraint);
            //auto e = Selection::FirstEntity();
            static bool ccd_succeed = false, fab_succeed = false;
            //auto &r = g_pSceneMgr->ActiveScene()->GetRegister();
            if (e != ECS::kInvalidEntity)
            {
                Solver::s_global_goal = r.GetComponent<TransformComponent>(e)->_transform;
                ccd_succeed = ccd_solver.Solve(Solver::s_global_goal);
                fab_succeed = fabrik_solver.Solve(Solver::s_global_goal);
            }
            //if (ImGui::Button("Solve"))
            //{
            //    ccd_solver.Clear();
            //    ccd_solver.Resize(4);
            //    ccd_solver[0]._position = Vector3f(0, 9, 0);
            //    ccd_solver[0]._rotation = Quaternion(0, 0, 1, 0);
            //    ccd_solver[1]._position = Vector3f(0, 3, 0);
            //    //ccd_solver[1]._rotation = Quaternion(0, 0, 0, 0);
            //    ccd_solver[2]._position = Vector3f(0, 3, 0);
            //    ccd_solver[3]._position = Vector3f(0, 0, 1);
            //    //ccd_solver[2]._rotation = Quaternion(0, 0, 1, 0);
            //    fabrik_solver.Clear();
            //    fabrik_solver.Resize(4);
            //    fabrik_solver[0]._position = Vector3f(5, 9, 0);
            //    fabrik_solver[1]._position = Vector3f(0, -3, 0);
            //    fabrik_solver[2]._position = Vector3f(0, -3, 0);
            //    fabrik_solver[3]._position = Vector3f(0, 0, 1);
            //    Transform t;
            //    if (e != ECS::kInvalidEntity)
            //    {
            //        ccd_succeed = ccd_solver.Solve(Solver::s_global_goal);
            //        fab_succeed = fabrik_solver.Solve(Solver::s_global_goal);
            //    }
            //    else
            //    {
            //        t._position = Vector3f(0,3.77,1.44);
            //        ccd_succeed = ccd_solver.Solve(t);
            //        t._position = Vector3f(5, 3.77, 1.44);
            //        fab_succeed = fabrik_solver.Solve(t);
            //    }
            //}
            int step = Solver::s_global_step_num;
            ImGui::SliderInt("StepNum", &step, 0, 30);
            Solver::s_global_step_num = step;
            u32 index = 0u;
            for (auto &sk_comp: r.View<CSkeletonMesh>())
            {
                auto tag = r.GetComponent<CSkeletonMesh, TagComponent>(index++);
                for (auto &it: sk_comp._p_mesh->GetSkeleton().GetSolvers())
                {
                    auto &[name, slover] = it;
                    ImGui::Text("Slover: %s", name.c_str());
                    {
                        ImGui::Indent();
                        //ccd_succeed ? ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)) : ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                        //ImGui::Text("%s", ccd_succeed ? "succeed" : "failed");
                        //ImGui::PopStyleColor();
                        for (u16 i = 0; i < slover->Size(); i++)
                        {
                            ImGui::Text("chain %d", i);
                            ImGui::Indent();
                            ImGui::Text("Pos: %s", (*slover)[i]._position.ToString().c_str());
                            ImGui::Text("Rot: %s", Quaternion::EulerAngles((*slover)[i]._rotation).ToString().c_str());
                            ImGui::Unindent();
                        }
                        ImGui::Text("Constraints: ");
                        ImGui::Indent();
                        u16 index = 0u;
                        for (auto &tit: slover->GetConstraints())
                        {
                            if (tit.empty())
                                ImGui::Button("Add");
                            else
                            {
                                for (auto cons: tit)
                                {
                                    if (auto p = dynamic_cast<IKConstraint *>(cons); p != nullptr)
                                    {
                                        ImGui::Text("Index: %d", index);
                                        ImGui::PushID(index++);
                                        Vector2f x, y, z;
                                        p->GetLimit(x, y, z);
                                        ImGui::SliderFloat2("X-axis", x.data, -180.f, 180.f, "%.0f");
                                        ImGui::SliderFloat2("Y-axis", y.data, -180.f, 180.f, "%.0f");
                                        ImGui::SliderFloat2("Z-axis", z.data, -180.f, 180.f, "%.0f");
                                        p->SetLimit(x, y, z);
                                        ImGui::PopID();
                                    }
                                }
                            }
                        }
                        ImGui::Unindent();
                        ImGui::Unindent();
                    }
                }
            }


            
            ImGui::Text("CCD:");
            {
                ImGui::Indent();
                ccd_succeed ? ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)) : ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                ImGui::Text("%s", ccd_succeed ? "succeed" : "failed");
                ImGui::PopStyleColor();
                for (u16 i = 0; i < ccd_solver.Size(); i++)
                {
                    ImGui::Text("chain %d", i);
                    ImGui::Indent();
                    ImGui::Text("Pos: %s", ccd_solver[i]._position.ToString().c_str());
                    ImGui::Text("Rot: %s", Quaternion::EulerAngles(ccd_solver[i]._rotation).ToString().c_str());
                    ImGui::Unindent();
                }
                ImGui::Unindent();
            }
            ImGui::Text("Fab:");
            {
                ImGui::Indent();
                fab_succeed ? ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)) : ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                ImGui::Text("%s", fab_succeed ? "succeed" : "failed");
                ImGui::PopStyleColor();
                for (u16 i = 0; i < fabrik_solver.Size(); i++)
                {
                    ImGui::Text("chain %d", i);
                    ImGui::Indent();
                    ImGui::Text("Pos: %s", fabrik_solver[i]._position.ToString().c_str());
                    ImGui::Text("Rot: %s", Quaternion::EulerAngles(fabrik_solver[i]._rotation).ToString().c_str());
                    ImGui::Unindent();
                }
                ImGui::Unindent();
            }
            ImGui::End();
            */

            if (s_show_imguinode)
                ShowImNodeTest(&s_show_imguinode);
        }

        void EditorLayer::Begin()
        {
        }

        void EditorLayer::End()
        {
            ImGuiWidget::EndFrame();
        }
    }// namespace Editor
}// namespace Ailu
