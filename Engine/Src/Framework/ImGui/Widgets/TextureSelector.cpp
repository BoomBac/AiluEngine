#include "pch.h"
#include "Framework/ImGui/Widgets/TextureSelector.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Ext/imgui/imgui_internal.h"
#include "Render/Texture.h"

namespace Ailu
{
	TextureSelector::TextureSelector() : ImGuiWidget("TextureSelector")
	{

	}
	TextureSelector::~TextureSelector()
	{
	}
	void TextureSelector::ShowImpl()
	{
		static float preview_tex_size = 128;
		u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
		int numImages = 10, imagesPerRow = window_width / (u32)preview_tex_size;
		imagesPerRow += imagesPerRow == 0 ? 1 : 0;
		static ImVec2 uv0{ 0,0 }, uv1{ 1,1 };
		int tex_count = 0;
		for (auto& [tex_name, tex] : TexturePool::GetAllResourceMap())
		{
			if (tex->GetTextureType() == ETextureType::kTexture2D)
			{
				ImGui::BeginGroup();
				ImGuiContext* context = ImGui::GetCurrentContext();
				auto drawList = context->CurrentWindow->DrawList;
				if (ImGui::ImageButton(tex->GetGPUNativePtr(), ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0))
				{
					_selected_img_index = tex_count;
					LOG_INFO("selected img {}", _selected_img_index);
				}
				if (_selected_img_index == tex_count)
				{
					ImVec2 cur_img_pos = ImGui::GetCursorPos();
					ImVec2 imgMin = ImGui::GetItemRectMin();
					ImVec2 imgMax = ImGui::GetItemRectMax();
					drawList->AddRect(imgMin, imgMax, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);
					_cur_selected_texture_path = tex_name;
				}
				ImGui::Text("%s", tex->Name().c_str());
				ImGui::EndGroup();
				if ((tex_count + 1) % imagesPerRow != 0)
				{
					ImGui::SameLine();
				}
				++tex_count;
			}
		}	
	}
	void TextureSelector::Open(const i32& handle)
	{
		if (handle != _handle)
			_cur_selected_texture_path = kNull;
		ImGuiWidget::Open(handle);
		_selected_img_index = -1;
	}
	void TextureSelector::Close()
	{
		ImGuiWidget::Close();
	}
	const String& TextureSelector::GetSelectedTexture(i32 handle) const
	{
		return _handle == handle ? _cur_selected_texture_path : kNull;
	}
}
