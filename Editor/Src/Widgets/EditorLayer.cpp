#include "Widgets/EditorLayer.h"
#include "Common/Selection.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"

#include "Framework/Common/TimeMgr.h"
#include "Render/Gizmo.h"
#include "Render/RenderingData.h"
#include <Framework/Common/Application.h>
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"

#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"

#include "Framework/Common/Profiler.h"
#include "Render/RenderPipeline.h"

#include "Common/Undo.h"
#include "Framework/Events/KeyEvent.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/AssetTable.h"
#include "Widgets/CommonTextureWidget.h"
#include "Widgets/EnvironmentSetting.h"
#include "Widgets/ObjectDetail.h"
#include "Widgets/OutputLog.h"
#include "Widgets/PlaceActors.h"
#include "Widgets/RenderView.h"
#include "Widgets/WorldOutline.h"
#include "Render/CommonRenderPipeline.h"

namespace Ailu
{
    namespace Editor
    {
        //static SceneActor* s_cur_selected_actor = nullptr;
        //static int cur_object_index = 0u;
        //static u32 cur_tree_node_index = 0u;
        //static u16 s_mini_tex_size = 64;

        //namespace TreeStats
        //{
        //	static ImGuiTreeNodeFlags s_base_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        //	static int s_selected_mask = 0;
        //	static bool s_b_selected = (s_selected_mask & (1 << cur_tree_node_index)) != 0;
        //	static int s_node_clicked = -1;
        //	static u32 s_pre_frame_selected_actor_id = 10;
        //	static u32 s_cur_frame_selected_actor_id = 0;
        //	static i16 s_cur_opened_texture_selector = -1;
        //	static bool s_is_texture_selector_open = true;
        //	static u32 s_common_property_handle = 0u;
        //	static String s_texture_selector_ret = "none";
        //}


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
            for (auto &widget: _widgets)
            {
                widget->Open(ImGuiWidget::GetGlobalWidgetHandle());
            }
            //默认不显示纹理细节窗口
            tex_detail_view->Close(tex_detail_view->Handle());
            _p_preview_cam_view->Close(_p_preview_cam_view->Handle());
            ImGuiWidget::SetFocus("AssetBrowser");
            auto r = static_cast<CommonRenderPipeline *>(g_pGfxContext->GetPipeline())->GetRenderer();
            r->AddFeature(&_pick);
            r->SetShadingMode(EShadingMode::kLit);
        }

        void EditorLayer::OnDetach()
        {
        }

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
                        g_pLogMgr->Log("Save assets...");
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
            }
            else if (e.GetEventType() == EEventType::kMouseButtonPressed)
            {
                MouseButtonPressedEvent &event = static_cast<MouseButtonPressedEvent &>(e);
                if (event.GetButton() == AL_KEY_LBUTTON)
                {
                    auto m_pos = Input::GetMousePos();
                    if (_p_scene_view->Hover(m_pos))
                    {
                        m_pos = m_pos - _p_scene_view->ContentPosition();
                        ECS::Entity closest_entity = _pick.GetPickID(m_pos.x, m_pos.y);
                        if (Input::IsKeyPressed(AL_KEY_SHIFT))
                            Selection::AddSelection(closest_entity);
                        else
                            Selection::AddAndRemovePreSelection(closest_entity);
                        if (closest_entity == ECS::kInvalidEntity)
                            Selection::RemoveSlection();
                    }
                }
            }
        }

        void EditorLayer::OnUpdate(f32 dt)
        {

        }

        static bool show = false;
        static bool s_show_asset_table = false;
        static bool s_show_rt = false;
        static bool s_show_renderview = true;

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
                ImGui::EndMainMenuBar();
            }
            ImGui::SetNextWindowSizeConstraints(ImVec2(100, 0), ImVec2(FLT_MAX, ImGui::GetTextLineHeight()));
            ImGui::Begin("ToolBar", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            auto app = Application::GetInstance();
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
            ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
            ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
            ImGui::Text("Draw Call: %d", RenderingStates::s_draw_call);
            ImGui::Text("VertCount: %d", RenderingStates::s_vertex_num);
            ImGui::Text("TriCount: %d", RenderingStates::s_triangle_num);
            if (ImGui::CollapsingHeader("Features"))
            {
                for (auto &feature: g_pGfxContext->GetPipeline()->GetRenderer()->GetFeatures())
                {
                    bool active = feature->IsActive();
                    ImGui::Checkbox(feature->Name().c_str(), &active);
                    feature->SetActive(active);
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
            static bool s_show_pass = false;
            if (ImGui::CollapsingHeader("Passes"))
            {
                for (auto &pass: g_pGfxContext->GetPipeline()->GetRenderer()->GetRenderPasses())
                {
                    bool active = pass->IsActive();
                    ImGui::Checkbox(pass->GetName().c_str(), &active);
                    pass->SetActive(active);
                }
            }
            static bool s_show_time_info = false;
            if (ImGui::CollapsingHeader("Performace Statics"))
            {
                String space = "";
                ImGui::Text("CPU Time:");
                while (!Profiler::g_Profiler._cpu_profiler_queue.empty())
                {
                    auto [is_start, profiler_id] = Profiler::g_Profiler._cpu_profiler_queue.front();
                    Profiler::g_Profiler._cpu_profiler_queue.pop();
                    if (is_start)
                    {
                        space.append("-");
                        const auto &profiler = Profiler::g_Profiler.GetCPUProfileData(profiler_id);
                        ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
                    }
                    else
                        space = space.substr(0, space.size() - 1);
                }
                space = "";
                ImGui::Text("GPU Time:");
                while (!Profiler::g_Profiler._gpu_profiler_queue.empty())
                {
                    auto [is_start, profiler_id] = Profiler::g_Profiler._gpu_profiler_queue.front();
                    Profiler::g_Profiler._gpu_profiler_queue.pop();
                    if (is_start)
                    {
                        space.append("-");
                        const auto &profiler = Profiler::g_Profiler.GetProfileData(profiler_id);
                        ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
                    }
                    else
                        space = space.substr(0, space.size() - 1);
                }
            }
            //			static const char* items[] = { "Shadering", "WireFrame", "ShaderingWireFrame" };
            //			static int item_current_idx = 0; // Here we store our selection data as an index.
            //			const char* combo_preview_value = items[item_current_idx];  // Pass in the preview value visible before opening the combo (it could be anything)
            //			if (ImGui::BeginCombo("Shadering Mode", combo_preview_value, 0))
            //			{
            //				for (int n = 0; n < IM_ARRAYSIZE(items); n++)
            //				{
            //					const bool is_selected = (item_current_idx == n);
            //					if (ImGui::Selectable(items[n], is_selected))
            //						item_current_idx = n;
            //					if (is_selected)
            //						ImGui::SetItemDefaultFocus();
            //				}
            //				ImGui::EndCombo();
            //			}
            //			if (item_current_idx == 0) RenderingStates::s_shadering_mode = EShaderingMode::kShader;
            //			else if (item_current_idx == 1) RenderingStates::s_shadering_mode = EShaderingMode::kWireFrame;
            //			else RenderingStates::s_shadering_mode = EShaderingMode::kShaderedWireFrame;

            ImGui::SliderFloat("Gizmo Alpha:", &Gizmo::s_color.a, 0.01f, 1.0f, "%.2f");
            ImGui::SliderFloat("Game Time Scale:", &TimeMgr::s_time_scale, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("ShadowDistance m", &QuailtySetting::s_main_light_shaodw_distance, 0.f, 100.0f, "%.2f");

            //g_pRenderer->_shadow_distance = shadow_dis_m * 100.0f;
            ImGui::Checkbox("Expand", &show);
            ImGui::Checkbox("ShowAssetTable", &s_show_asset_table);
            ImGui::Checkbox("ShowRT", &s_show_rt);
            ImGui::Checkbox("ShowUndoView", &s_show_undo_view);

            for (auto &info: g_pResourceMgr->GetImportInfos())
            {
                float x = g_pTimeMgr->GetScaledWorldTime(0.25f);
                ImGui::Text("%s", info._msg.c_str());
                ImGui::SameLine();
                ImGui::ProgressBar(x - static_cast<int>(x), ImVec2(0.f, 0.f));
            }
            ImGui::End();
            if (show)
                ImGui::ShowDemoWindow(&show);
            ECS::Entity e = Selection::FirstEntity();
            if (e != ECS::kInvalidEntity)
            {
                if (g_pSceneMgr->ActiveScene()->GetRegister().HasComponent<CCamera>(e))
                {
                    static_cast<RenderView *>(_p_preview_cam_view)->SetSource(g_pGfxContext->GetPipeline()->GetTarget(1));
                    _p_preview_cam_view->Open(_p_preview_cam_view->Handle());
                }
                else
                {
                    _p_preview_cam_view->Close(_p_preview_cam_view->Handle());
                }
            }
            auto scene_rt = g_pGfxContext->GetPipeline()->GetTarget(0);
            static_cast<SceneView *>(_p_scene_view)->SetSource(scene_rt);

            for (auto &widget: _widgets)
            {
                widget->Show();
            }
            ImGuiWidget::ShowProgressBar();
            if (s_show_undo_view)
            {
                ImGui::Begin("UndoView", &s_show_undo_view);
                for (auto cmd: g_pCommandMgr->UndoViews())
                {
                    ImGui::Text("%s",cmd->ToString().c_str());
                }
                ImGui::End();
            }
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
