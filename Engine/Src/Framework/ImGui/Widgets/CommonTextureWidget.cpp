#include "pch.h"
#include "Framework/ImGui/Widgets/CommonTextureWidget.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Ext/imgui/imgui_internal.h"
#include "Render/Texture.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Assert.h"

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
		u64 tex_num = g_pTexturePool->Size();
		for (auto& [tex_name, tex] : *g_pTexturePool)
		{
			if (tex->Dimension() == ETextureDimension::kTex2D)
			{
				TextureHandle cur_tex_handle = tex->GetNativeTextureHandle();
				if (cur_tex_handle == 0)
				{
					g_pLogMgr->LogErrorFormat(L"{} tex handle is 0",tex_name);
					continue;
				}
				ImGui::BeginGroup();
				ImGuiContext* context = ImGui::GetCurrentContext();
				auto drawList = context->CurrentWindow->DrawList;
				if (ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(cur_tex_handle), ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0))
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
					_cur_selected_texture_id = tex->ID();
				}
				ImGui::Text("%s", tex->Name().c_str());
				ImGui::EndGroup();
				if (tex_count + 1 != tex_num && (tex_count + 1) % imagesPerRow != 0)
				{
					ImGui::SameLine();
				}
				++tex_count;
			}
		}
		static const char* mipmaps[] = {"Main","mipmap1","mipmap2" };
		static int s_mipmap_index = 0;
		if (ImGui::BeginCombo("select mipmap", mipmaps[s_mipmap_index]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(mipmaps); n++)
			{
				const bool is_selected = (s_mipmap_index == n);
				if (ImGui::Selectable(mipmaps[n], is_selected))
					s_mipmap_index = n;
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		//ImGui::ImageButton(reinterpret_cast<void*>(s_mipmap_index == 0 ? g_pResourceMgr->_test_new_tex->GetNativeTextureHandle() : g_pResourceMgr->_test_new_tex->GetView(s_mipmap_index)),
		//	ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0);

		//static const char* s_faces[] = { "+Y", "-X", "+Z", "+X","-Z","-Y" };
		//static int s_face_index = 0;
		//if (ImGui::BeginCombo("select mipmap face", s_faces[s_face_index]))
		//{
		//	for (int n = 0; n < IM_ARRAYSIZE(s_faces); n++)
		//	{
		//		const bool is_selected = (s_face_index == n);
		//		if (ImGui::Selectable(s_faces[n], is_selected))
		//			s_face_index = n;
		//		if (is_selected)
		//			ImGui::SetItemDefaultFocus();
		//	}
		//	ImGui::EndCombo();
		//}
		//ImGui::ImageButton(reinterpret_cast<void*>(g_pResourceMgr->_test_cubemap->GetView((ECubemapFace::ECubemapFace)s_face_index, s_mipmap_index)),
		//	ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0);
	}
	void TextureSelector::Open(const i32& handle)
	{
		if (handle != _handle)
			_cur_selected_texture_id = kInvalidTextureID;
		ImGuiWidget::Open(handle);
		_selected_img_index = -1;
	}
	void TextureSelector::Close(i32 handle)
	{
		ImGuiWidget::Close(handle);
	}
	u64 TextureSelector::GetSelectedTexture(i32 handle) const
	{
		return _handle == handle ? _cur_selected_texture_id : kInvalidTextureID;
	}
	//--------------------------------------------------------------------------------------------RenderTextureView------------------------------------------------------------------------------------------
	RenderTextureView::RenderTextureView() : ImGuiWidget("RenderTextureView")
	{
	}
	RenderTextureView::~RenderTextureView()
	{
	}
	void RenderTextureView::Open(const i32& handle)
	{
		ImGuiWidget::Open(handle);
	}
	void RenderTextureView::Close(i32 handle)
	{
		ImGuiWidget::Close(handle);
	}
	void RenderTextureView::ShowImpl()
	{
		static float preview_tex_size = 128;
		u32 window_width = (u32)ImGui::GetWindowContentRegionWidth();
		int numImages = 10, imagesPerRow = window_width / (u32)preview_tex_size;
		imagesPerRow += imagesPerRow == 0 ? 1 : 0;
		static ImVec2 uv0{ 0,0 }, uv1{ 1,1 };
		int tex_count = 0;
		u64 tex_num = g_pRenderTexturePool->Size();
		for (auto& [rt_hash,rt_info] : *g_pRenderTexturePool)
		{
			auto tex = rt_info._rt.get();
			if (tex->Dimension() == ETextureDimension::kTex2D)
			{
				TextureHandle cur_tex_handle = tex->GetNativeTextureHandle();
				AL_ASSERT(cur_tex_handle == 0);
				ImGui::BeginGroup();
				//ImGuiContext* context = ImGui::GetCurrentContext();
				//auto drawList = context->CurrentWindow->DrawList;
				//if (ImGui::ImageButton(TEXTURE_HANDLE_TO_IMGUI_TEXID(cur_tex_handle), ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0))
				//{
				//}
				ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(cur_tex_handle), ImVec2(preview_tex_size, preview_tex_size), uv0, uv1);
				ImGui::Text(tex->Name().c_str());
				ImGui::EndGroup();
				if (tex_count + 1 != tex_num && (tex_count + 1) % imagesPerRow != 0)
				{
					ImGui::SameLine();
				}
				++tex_count;
			}
		}
		static const char* mipmaps[] = { "Main","mipmap1","mipmap2" };
		static int s_mipmap_index = 0;
		if (ImGui::BeginCombo("select mipmap", mipmaps[s_mipmap_index]))
		{
			for (int n = 0; n < IM_ARRAYSIZE(mipmaps); n++)
			{
				const bool is_selected = (s_mipmap_index == n);
				if (ImGui::Selectable(mipmaps[n], is_selected))
					s_mipmap_index = n;
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		//ImGui::ImageButton(reinterpret_cast<void*>(s_mipmap_index == 0 ? g_pResourceMgr->_test_new_tex->GetNativeTextureHandle() : g_pResourceMgr->_test_new_tex->GetView(s_mipmap_index)),
		//	ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0);

		//static const char* s_faces[] = { "+Y", "-X", "+Z", "+X","-Z","-Y" };
		//static int s_face_index = 0;
		//if (ImGui::BeginCombo("select mipmap face", s_faces[s_face_index]))
		//{
		//	for (int n = 0; n < IM_ARRAYSIZE(s_faces); n++)
		//	{
		//		const bool is_selected = (s_face_index == n);
		//		if (ImGui::Selectable(s_faces[n], is_selected))
		//			s_face_index = n;
		//		if (is_selected)
		//			ImGui::SetItemDefaultFocus();
		//	}
		//	ImGui::EndCombo();
		//}
		//ImGui::ImageButton(reinterpret_cast<void*>(g_pResourceMgr->_test_cubemap->GetView((ECubemapFace::ECubemapFace)s_face_index, s_mipmap_index)),
		//	ImVec2(preview_tex_size, preview_tex_size), uv0, uv1, 0);
	}
	//--------------------------------------------------------------------------------------------RenderTextureView------------------------------------------------------------------------------------------

}
