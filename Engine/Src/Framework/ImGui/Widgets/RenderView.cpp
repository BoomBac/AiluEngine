#include "pch.h"
#include "Framework/ImGui/Widgets/RenderView.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Input.h"
#include "Render/CommandBuffer.h"
#include "Render/Renderer.h"

namespace Ailu
{
	RenderView::RenderView() : ImGuiWidget("RenderView")
	{
		_is_hide_common_widget_info = true;
	}
	RenderView::~RenderView()
	{
	}
	void RenderView::Open(const i32& handle)
	{
		ImGuiWidget::Open(handle);
	}
	void RenderView::Close()
	{
		ImGuiWidget::Close();
	}
	void RenderView::ShowImpl()
	{
		auto rt = g_pRenderer->GetTargetTexture();
		if (rt)
		{
			ImVec2 vMin = ImGui::GetWindowContentRegionMin();
			ImVec2 vMax = ImGui::GetWindowContentRegionMax();
			vMin.x += ImGui::GetWindowPos().x;
			vMin.y += ImGui::GetWindowPos().y;
			vMax.x += ImGui::GetWindowPos().x;
			vMax.y += ImGui::GetWindowPos().y;
			ImVec2 size = vMax - vMin;
			auto cmd = CommandBufferPool::Get();
			rt->Transition(cmd.get(), ETextureResState::kShaderResource);
			g_pGfxContext->ExecuteCommandBuffer(cmd);
			ImGui::Image(rt->GetGPUNativePtr(), size);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
			{
				_is_focus = true;
				ImGui::SetKeyboardFocusHere();
			}
		}
		else
		{
			ImGui::LabelText("RenderView", "No Render Target");
		}
		Input::s_block_input = !_is_focus;
	}
}
