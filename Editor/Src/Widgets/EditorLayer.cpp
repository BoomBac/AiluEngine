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
#include "Render/Features/PostprocessPass.h"
#include "Render/Features/VolumetricClouds.h"
#include "Render/RenderingData.h"
#include "UI/TextRenderer.h"
//#include <Framework/Common/Application.h>
#include <Objects/Type.h>

#include "Inc/EditorApp.h"

#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"

#include "Framework/Common/Profiler.h"
#include "Render/RenderPipeline.h"

#include "UI/UIRenderer.h"
#include "UI/Widget.h"
#include "UI/Container.h"
#include "UI/Basic.h"
#include "UI/UILayer.h"
#include "UI/UIFramework.h"

#include "Objects/JsonArchive.h"

#include "Animation/Curve.hpp"
#include "Animation/Solver.h"
#include "Common/Undo.h"
#include "Framework/Events/KeyEvent.h"
#include "Render/CommonRenderPipeline.h"

#include "Widgets/RenderView.h"
#include "Widgets/CommonView.h"

#include "Framework/Parser/TextParser.h"

namespace Ailu
{
    using namespace Render;
    namespace Editor
    {
        EditorLayer::EditorLayer() : Layer("EditorLayer")
        {
#if defined(DEAR_IMGUI)
            ImGuiIO &io = ImGui::GetIO();
            (void) io;
            //_font = io.Fonts->AddFontFromFileTTF(ResourceMgr::GetResSysPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
            _font = io.Fonts->AddFontFromFileTTF(ResourceMgr::GetResSysPath("Fonts/Open_Sans/static/OpenSans-Regular.ttf").c_str(), 14.0f);
            io.Fonts->Build();
#endif
        }

        EditorLayer::EditorLayer(const String &name) : Layer(name)
        {
        }

        EditorLayer::~EditorLayer()
        {

        }
        UI::Text* g_text = nullptr;
        static void FindFirstText(UI::Widget* w)
        {
            for(auto c : *w)
            {
                if (c->GetType() == UI::Text::StaticType())
                {
                    g_text = dynamic_cast<UI::Text*>(c.get());
                    break;
                }
            }
        }

        UI::UILayer* g_ui_layer;
        void EditorLayer::OnAttach()
        {
            auto r = Render::RenderPipeline::Get().GetRenderer();
            r->AddFeature(&_pick);
            r->SetShadingMode(EShadingMode::kLit);

            //auto root = MakeRef<UI::Canvas>();
            //auto vb = MakeRef<UI::VerticalBox>();
            //vb->AddChild(MakeRef<UI::Button>())->OnMouseClick() += [](UI::UIEvent &e)
            //{ LOG_INFO("Clicked!"); };
            //dynamic_cast<UI::Text*>(root->AddChild(MakeRef<UI::Text>()))->_text = "This is a text";
            //vb->AddChild(MakeRef<UI::Slider>());
            //root->AddChild(vb);
            //_main_widget->AddToWidget(root);
            DockManager::Init();

            //auto testw = MakeRef<RenderView>();
            //testw->SetPosition({200.0f, 200.0f});
            //_scene_view = testw.get();
            //DockManager::Get().AddDock(testw);
            //auto common_view = MakeRef<CommonView>();
            //common_view->SetPosition({200.0f, 200.0f});
            //_common_view = common_view.get();
            //DockManager::Get().AddDock(common_view);
            
            _main_widget = MakeRef<UI::Widget>();
            
            auto p = Application::Get().GetUseHomePath() + L"OneDrive/AiluEngine/Editor/Res/UI/main_widget.json";
            JsonArchive ar;
            ar.Load(p);
            ar >> *_main_widget;

            //auto root = MakeRef<UI::Canvas>();
            //auto vb = MakeRef<UI::VerticalBox>();
            //vb->AddChild(MakeRef<UI::Button>())->OnMouseClick() += [](UI::UIEvent &e)
            //{ LOG_INFO("Clicked!"); };
            //vb->AddChild<UI::Text>()->SetText("This is a text");
            //vb->AddChild(MakeRef<UI::Slider>());
            //auto cv = vb->AddChild<UI::CollapsibleView>("CollapsibleView");
            //{
            //    auto cvb = MakeRef<UI::VerticalBox>();
            //    cvb->AddChild<UI::Button>();
            //    cvb->AddChild<UI::Text>()->SetText("CVB: This is a text");
            //    cvb->AddChild(MakeRef<UI::Slider>());
            //    cv->GetContent()->AddChild(cvb);
            //}
            //root->AddChild(vb);
            //_main_widget->AddToWidget(root);
            //JsonArchive ar;
            //ar << *_main_widget;
            //auto p = Application::Get().GetUseHomePath() + L"OneDrive/AiluEngine/Editor/Res/UI/main_widget.json";
            //LOG_INFO(L"Save widget to ", p);
            //ar.Save(p);

            //UI::UIManager::Get()->RegisterWidget(_main_widget);
            FindFirstText(_main_widget.get());
            dynamic_cast<EditorApp &>(Application::Get())._on_file_changed += [&](const fs::path &file)
            {
                const WString cur_path = PathUtils::FormatFilePath(file.wstring());
                if (PathUtils::GetFileName(cur_path) == L"main_widget")
                {
                    LOG_INFO(L"Reload widget {}",cur_path);
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
                    if (cur_type->BaseType() && cur_type->BaseType() != cur_type)//除去Object
                    {
                        //for (auto prop: cur_type->GetProperties())
                        //{
                        //    DrawMemberProperty(prop, *feature);
                        //}
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
                    auto& [name, time] = it;
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
            }
            static RenderTexture* s_last_scene_rt = nullptr;
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
        }

        void EditorLayer::Begin()
        {
        }

        void EditorLayer::End()
        {

        }
    }// namespace Editor
}// namespace Ailu
