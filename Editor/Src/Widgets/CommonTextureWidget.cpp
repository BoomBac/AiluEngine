#include "Widgets/CommonTextureWidget.h"
#include "Ext/imgui/backends/imgui_impl_dx12.h"
#include "Ext/imgui/imgui.h"
#include "Ext/imgui/imgui_internal.h"
#include "Framework/Common/Assert.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Texture.h"

namespace Ailu
{
    namespace Editor
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
            ImVec2 vMin = ImGui::GetWindowContentRegionMin();
            ImVec2 vMax = ImGui::GetWindowContentRegionMax();
            u32 window_width = (u32) (vMax.x - vMin.x);
            int numImages = 10, imagesPerRow = window_width / (u32) preview_tex_size;
            imagesPerRow += imagesPerRow == 0 ? 1 : 0;
            static ImVec2 uv0{0, 0}, uv1{1, 1};
            int tex_count = 0;
            u64 tex_num = g_pResourceMgr->TotalNum<Texture2D>();
            for (auto it = g_pResourceMgr->ResourceBegin<Texture2D>(); it != g_pResourceMgr->ResourceEnd<Texture2D>(); it++)
            {
                auto tex = ResourceMgr::IterToRefPtr<Texture2D>(it);
                const String &tex_name = tex->Name();
                if (tex->Dimension() == ETextureDimension::kTex2D)
                {
                    TextureHandle cur_tex_handle = tex->GetNativeTextureHandle();
                    if (cur_tex_handle == 0)
                    {
                        g_pLogMgr->LogErrorFormat("{} tex handle is 0", tex_name);
                        continue;
                    }
                    ImGui::BeginGroup();
                    ImGuiContext *context = ImGui::GetCurrentContext();
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
            static const char *mipmaps[] = {"Main", "mipmap1", "mipmap2"};
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
        void TextureSelector::Open(const i32 &handle)
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
        u32 TextureSelector::GetSelectedTexture(i32 handle) const
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
        void RenderTextureView::Open(const i32 &handle)
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
            ImVec2 vMin = ImGui::GetWindowContentRegionMin();
            ImVec2 vMax = ImGui::GetWindowContentRegionMax();
            u32 window_width = (u32) (vMax.x - vMin.x);
            int numImages = 10, imagesPerRow = window_width / (u32) preview_tex_size;
            imagesPerRow += imagesPerRow == 0 ? 1 : 0;
            static ImVec2 uv0{0, 0}, uv1{1, 1};
            int tex_count = 0;
            u64 tex_num = g_pRenderTexturePool->Size();
            for (auto it = g_pRenderTexturePool->PersistentRTBegin(); it != g_pRenderTexturePool->PersistentRTEnd(); it++)
            {
                auto tex = it->second;
                if (tex->Dimension() == ETextureDimension::kTex2D)
                {
                    TextureHandle cur_tex_handle = tex->GetNativeTextureHandle();
                    if (cur_tex_handle == 0)
                        continue;
                    AL_ASSERT(cur_tex_handle == 0);
                    ImGui::BeginGroup();
                    ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(cur_tex_handle), ImVec2(preview_tex_size, preview_tex_size), uv0, uv1);
                    ImGui::EndGroup();
                    if (tex_count + 1 != tex_num && (tex_count + 1) % imagesPerRow != 0)
                    {
                        ImGui::SameLine();
                    }
                }
                else if (tex->Dimension() == ETextureDimension::kCube)
                {
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
                else if (tex->Dimension() == ETextureDimension::kTex2DArray)
                {
                    for (u16 i = 0; i < tex->ArraySlice(); i++)
                    {
                        TextureHandle cur_tex_handle = tex->GetView(Texture::ETextureViewType::kSRV, 0, i);
                        if (cur_tex_handle == 0)
                            continue;
                        AL_ASSERT(cur_tex_handle == 0);
                        ImGui::BeginGroup();
                        ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(cur_tex_handle), ImVec2(preview_tex_size, preview_tex_size), uv0, uv1);
                        ImGui::EndGroup();
                        ImGui::SameLine();
                        ++tex_count;
                    }
                }
                ImGui::Text("Name: %s,Dimension: %s", tex->Name().c_str(), ETextureDimension::ToString(tex->Dimension()));
            }
        }
        //--------------------------------------------------------------------------------------------RenderTextureView------------------------------------------------------------------------------------------

        //--------------------------------------------------------------------------------------------TextureDetailView------------------------------------------------------------------------------------------
        TextureDetailView::TextureDetailView() : ImGuiWidget("TextureDetailView")
        {
            _is_hide_common_widget_info = true;
            OnWidgetClose([this]()
                          {
				g_pLogMgr->LogWarning("TextureDetailView close");
				_p_tex = nullptr; });
        }

        TextureDetailView::~TextureDetailView()
        {
        }

        void TextureDetailView::Open(const i32 &handle)
        {
            ImGuiWidget::Open(handle);
        }

        void TextureDetailView::Open(const i32 &handle, Texture *tex)
        {
            if (!tex)
            {
                g_pLogMgr->LogWarning("Clocked asset is not texture");
                return;
            }

            ImGuiWidget::Open(handle);
            _p_tex = tex;
            _title = _p_tex->Name();
            _cur_mipmap_level = 0;
        }

        void TextureDetailView::Close(i32 handle)
        {
            ImGuiWidget::Close(handle);
            _p_tex = nullptr;
        }

        void TextureDetailView::ShowImpl()
        {
            if (_p_tex)
            {
                TextureHandle cur_tex_handle = _p_tex->GetView(Texture::ETextureViewType::kSRV, _cur_mipmap_level);
                if (cur_tex_handle == 0)
                {
                    //_p_tex->CreateView(Texture::ETextureViewType::kSRV,_cur_mipmap_level);
                    _p_tex->CreateView();
                }
                ImGui::Image(TEXTURE_HANDLE_TO_IMGUI_TEXID(cur_tex_handle), ImVec2(_size.x * 0.75f, _size.x * 0.75f));
                //ImGui::Spacing();
                ImGui::SameLine();
                ImGui::BeginGroup();

                ImGuiStyle &style = ImGui::GetStyle();
                float scrollbarWidth = style.ScrollbarSize;
                //ImGui::SetNextItemWidth(_size.x * 0.20f);
                ImGui::Text("Mip %d", _cur_mipmap_level);
                //ImGui::SetNextItemWidth(_size.x * 0.20f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(_size.x * 0.20f - scrollbarWidth);
                ImGui::SliderInt("##Mip", &_cur_mipmap_level, 0, _p_tex->MipmapLevel());

                ImGui::Text("Name: %s", _p_tex->Name().c_str());
                ImGui::Text("Improted: %d x %d", _p_tex->Width(), _p_tex->Height());
                auto [w, h] = _p_tex->CurMipmapSize(_cur_mipmap_level);
                ImGui::Text("Displayed: %d x %d", w, h);
                ImGui::Text("Mipmap: %d", _p_tex->MipmapLevel() + 1);
                ImGui::Text("Fomat: %s", EALGFormat::ToString(_p_tex->PixelFormat()));
                ImGui::Text("Graphics Memory: %.2f mb", (f32) _p_tex->GetGpuMemerySize() * 9.5367431640625E-07);
                //if (ImGui::CollapsingHeader("Common"))
                //{

                //}
                //if (ImGui::CollapsingHeader("Advance"))
                //{

                //}
                ImGui::EndGroup();
            }
        }
        //--------------------------------------------------------------------------------------------TextureDetailView------------------------------------------------------------------------------------------

    }// namespace Editor
}// namespace Ailu
