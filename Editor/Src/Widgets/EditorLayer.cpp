#include "Widgets/EditorLayer.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"

#include "Framework/Common/TimeMgr.h"
#include "Render/RenderingData.h"
#include "Render/Gizmo.h"

#include "Objects/CameraComponent.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/LogMgr.h"

#include "Framework/Events/MouseEvent.h"
#include "Framework/Common/Input.h"

#include "Render/RenderPipeline.h"
#include "Framework/Common/Profiler.h"

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
			ImGuiIO& io = ImGui::GetIO(); (void)io;
			_font = io.Fonts->AddFontFromFileTTF(PathUtils::GetResSysPath("Fonts/VictorMono-Regular.ttf").c_str(), 13.0f);
			io.Fonts->Build();
		}

		EditorLayer::EditorLayer(const String& name) : Layer(name)
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
			for (auto appender : g_pLogMgr->GetAppenders())
			{
				auto imgui_appender = dynamic_cast<ImGuiLogAppender*>(appender);
				if (imgui_appender)
				{
					_widgets.emplace_back(std::move(MakeScope<OutputLog>(imgui_appender)));
				}
			}

			_widgets.emplace_back(std::move(MakeScope<RenderView>()));
			_p_render_view = _widgets.back().get();
			_widgets.emplace_back(std::move(MakeScope<EnvironmentSetting>()));
			_p_env_setting = _widgets.back().get();
			for (auto& widget : _widgets)
			{
				widget->Open(ImGuiWidget::GetGlobalWidgetHandle());
			}
			//默认不显示纹理细节窗口
			tex_detail_view->Close(tex_detail_view->Handle());
			ImGuiWidget::SetFocus("AssetBrowser");
		}

		void EditorLayer::OnDetach()
		{

		}

		void EditorLayer::OnEvent(Event& e)
		{
            for (auto &w: _widgets)
                w->OnEvent(e);
            if (e.GetEventType() == EEventType::kKeyPressed)
            {
                auto &key_e = static_cast<KeyPressedEvent &>(e);
                if (key_e.GetKeyCode() == AL_KEY_F11)
                {
                    g_pGfxContext->GetPipeline()->GetRenderer()->TakeCapture();
                    //g_pLogMgr->Log("g_pRenderer->TakeCapture();");
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
                        if(Input::IsKeyPressed(AL_KEY_SHIFT))
                        {
                            g_pCommandMgr->Redo();
                        }
                        else
                        {
                            g_pCommandMgr->Undo();
                        }
                    }
                }
            }
		}

		static bool show = false;
		static bool s_show_asset_table = false;
		static bool s_show_rt = false;
		static bool s_show_renderview = true;

    void EditorLayer::OnImguiRender()
    {
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

        ImGui::Begin("Performace Statics");// Create a window called "Hello, world!" and append into it.
        ImGui::Text("FrameRate: %.2f", ImGui::GetIO().Framerate);
        ImGui::Text("FrameTime: %.2f ms", ModuleTimeStatics::RenderDeltatime);
        ImGui::Text("Draw Call: %d", RenderingStates::s_draw_call);
        ImGui::Text("VertCount: %d", RenderingStates::s_vertex_num);
        ImGui::Text("TriCount: %d", RenderingStates::s_triangle_num);
        for (auto& pass : g_pGfxContext->GetPipeline()->GetRenderer()->GetRenderPasses())
        {
            bool active = pass->IsActive();
            ImGui::Checkbox(pass->GetName().c_str(), &active);
            pass->SetActive(active);
        }

			String space = "";
			ImGui::Text("CPU Time:");
			while (!Profiler::g_Profiler._cpu_profiler_queue.empty())
			{
				auto [is_start, profiler_id] = Profiler::g_Profiler._cpu_profiler_queue.front();
				Profiler::g_Profiler._cpu_profiler_queue.pop();
				if (is_start)
				{
					space.append("-");
					const auto& profiler = Profiler::g_Profiler.GetCPUProfileData(profiler_id);
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
					const auto& profiler = Profiler::g_Profiler.GetProfileData(profiler_id);
					ImGui::Text("%s%s,%.2f ms", space.c_str(), profiler->Name.c_str(), profiler->_avg_time);
				}
				else
					space = space.substr(0, space.size() - 1);
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
        float shadow_dis_m = QuailtySetting::s_main_light_shaodw_distance / 100u;
        ImGui::SliderFloat("ShadowDistance m", &shadow_dis_m, 0.f, 100.0f, "%.2f");
        QuailtySetting::s_main_light_shaodw_distance = shadow_dis_m * 100;

        //g_pRenderer->_shadow_distance = shadow_dis_m * 100.0f;
        ImGui::Checkbox("Expand", &show);
        ImGui::Checkbox("ShowAssetTable", &s_show_asset_table);
        ImGui::Checkbox("ShowRT", &s_show_rt);
        for (auto &info: g_pResourceMgr->_import_infos)
        {
            float x = g_pTimeMgr->GetScaledWorldTime(0.25f);
            ImGui::Text("%s", info._msg.c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(x - static_cast<int>(x), ImVec2(0.f, 0.f));
        }
        ImGui::End();
        if (show)
            ImGui::ShowDemoWindow(&show);
        for (auto &widget: _widgets)
        {
            widget->Show();
        }
        ImGuiWidget::ShowProgressBar();
    }

		void EditorLayer::Begin()
		{

		}

		void EditorLayer::End()
		{
			ImGuiWidget::EndFrame();
		}
	}
}


