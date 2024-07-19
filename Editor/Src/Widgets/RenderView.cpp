#include "Widgets/RenderView.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Input.h"
#include "Render/CommandBuffer.h"
#include "Render/RenderPipeline.h"
#include "Framework/Common/Application.h"

namespace Ailu
{
	namespace Editor
	{
		RenderView::RenderView() : ImGuiWidget("RenderView")
		{
			_is_hide_common_widget_info = false;
			_allow_close = false;
		}
		RenderView::~RenderView()
		{
		}
		void RenderView::Open(const i32& handle)
		{
			ImGuiWidget::Open(handle);
		}
		void RenderView::Close(i32 handle)
		{
			ImGuiWidget::Close(handle);
		}
		void RenderView::ShowImpl()
		{
			auto rt = g_pGfxContext->GetPipeline()->CameraTarget();
			static const auto& cur_window = Application::GetInstance()->GetWindow();
			if (rt)
			{
				ImVec2 vMin = ImGui::GetWindowContentRegionMin();
				ImVec2 vMax = ImGui::GetWindowContentRegionMax();
				vMin.x += ImGui::GetWindowPos().x;
				vMin.y += ImGui::GetWindowPos().y;
				vMax.x += ImGui::GetWindowPos().x;
				vMax.y += ImGui::GetWindowPos().y;
				ImVec2 gameview_size;
				gameview_size.x = vMax.x - vMin.x;
				gameview_size.y = vMax.y - vMin.y;
				Vector2D<u32> window_size = Vector2D<u32>{ static_cast<u32>(cur_window.GetWidth()), static_cast<u32>(cur_window.GetHeight()) };
				// 设置窗口背景颜色
				ImGuiStyle& style = ImGui::GetStyle();
				auto origin_color = style.Colors[ImGuiCol_WindowBg];
				style.Colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // 设置窗口背景颜色为灰色
				ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(rt->ColorTexture()), gameview_size);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
				{
					_is_focus = true;
					ImGui::SetKeyboardFocusHere();
				}
				style.Colors[ImGuiCol_WindowBg] = origin_color;
				static bool s_is_begin_resize = false;
				static u32 s_resize_end_frame_count = 0;
				if (_pre_tick_width != gameview_size.x || _pre_tick_height != gameview_size.y)
				{
					s_is_begin_resize = true;
					if (!s_is_begin_resize)
					{
						_pre_tick_window_width = (f32)window_size.x;
						_pre_tick_window_height = (f32)window_size.y;
					}
				}
				else
				{
					if (s_is_begin_resize)
					{
						++s_resize_end_frame_count;
					}
				}
				if (s_resize_end_frame_count > Application::kTargetFrameRate * 0.5f)
				{
					//g_pRenderer->ResizeBuffer(static_cast<u32>(gameview_size.x), static_cast<u32>(gameview_size.y));
					if (_pre_tick_window_width != window_size.x || _pre_tick_window_height != window_size.y)
					{
						g_pGfxContext->ResizeSwapChain(window_size.x, window_size.y);
					}
					s_is_begin_resize = false;
					s_resize_end_frame_count = 0;
				}
				_pre_tick_width = gameview_size.x;
				_pre_tick_height = gameview_size.y;
				if (ImGui::BeginDragDropTarget())
				{
					const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kDragAssetMesh.c_str());
					if (payload)
					{
						auto mesh = *(Asset**)(payload->Data);
						g_pSceneMgr->AddSceneActor(mesh->AsRef<Mesh>());
					}
					ImGui::EndDragDropTarget();
				}
			}
			else
			{
				ImGui::LabelText("RenderView", "No Render Target");
			}
			Input::s_block_input = !_is_focus;
		}
	}
}
