#include "Widgets/EnvironmentSetting.h"

#include "Ext/imgui/imgui.h"
#include "Render/Renderer.h"

namespace Ailu
{
	namespace Editor
	{
		EnvironmentSetting::EnvironmentSetting() : ImGuiWidget("EnvironmentSetting")
		{

		}
		EnvironmentSetting::~EnvironmentSetting()
		{
		}
		void EnvironmentSetting::Open(const i32& handle)
		{
			ImGuiWidget::Open(handle);
			for (auto& pass : g_pRenderer->GetRenderPasses())
			{
				auto gbuffer_pass = dynamic_cast<DeferredGeometryPass*>(pass);
				if (gbuffer_pass)
				{
					_deferred_lighting_mat = gbuffer_pass->_p_lighting_material.get();
				}
			}
		}
		void EnvironmentSetting::Close(i32 handle)
		{
			ImGuiWidget::Close(handle);
		}

		static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
		{
			return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
		}
		static inline ImVec2 ImRotate(const ImVec2& v, float cos_a, float sin_a)
		{
			return ImVec2(v.x * cos_a - v.y * sin_a, v.x * sin_a + v.y * cos_a);
		}
		void ImageRotated(ImTextureID tex_id, ImVec2 center, ImVec2 size, float angle)
		{
			ImDrawList* draw_list = ImGui::GetWindowDrawList();

			float cos_a = cosf(angle);
			float sin_a = sinf(angle);
			ImVec2 pos[4] =
			{
				center + ImRotate(ImVec2(-size.x * 0.5f, -size.y * 0.5f), cos_a, sin_a),
				center + ImRotate(ImVec2(+size.x * 0.5f, -size.y * 0.5f), cos_a, sin_a),
				center + ImRotate(ImVec2(+size.x * 0.5f, +size.y * 0.5f), cos_a, sin_a),
				center + ImRotate(ImVec2(-size.x * 0.5f, +size.y * 0.5f), cos_a, sin_a)
			};
			ImVec2 uvs[4] =
			{
				ImVec2(0.0f, 0.0f),
				ImVec2(1.0f, 0.0f),
				ImVec2(1.0f, 1.0f),
				ImVec2(0.0f, 1.0f)
			};

			draw_list->AddImageQuad(tex_id, pos[0], pos[1], pos[2], pos[3], uvs[0], uvs[1], uvs[2], uvs[3], IM_COL32_WHITE);
		}

		void EnvironmentSetting::ShowImpl()
		{
			ImGui::Text("SkyboxSource:");
			static u16 s_selected_chechbox = -1;
			// 复选框1
			bool checkbox1 = (s_selected_chechbox == 0);
			if (ImGui::Checkbox("DEBUG_NORMAL", &checkbox1)) 
			{
				if (checkbox1) 
				{
					_deferred_lighting_mat->EnableKeyword("DEBUG_NORMAL");
					//Shader::SetGlobalTexture("SkyBox", g_pRenderer->_p_env_tex);
					s_selected_chechbox = 0;
				}
				else if (s_selected_chechbox == 0) 
				{
					_deferred_lighting_mat->DisableKeyword("DEBUG_NORMAL");
					s_selected_chechbox = -1;
				}
			}

			// 复选框2
			bool checkbox2 = (s_selected_chechbox == 1);
			if (ImGui::Checkbox("DEBUG_ALBEDO", &checkbox2)) 
			{
				if (checkbox2) 
				{
					_deferred_lighting_mat->EnableKeyword("DEBUG_ALBEDO");
					//Shader::SetGlobalTexture("SkyBox", g_pRenderer->_p_radiance_tex);
					s_selected_chechbox = 1;
				}
				else if (s_selected_chechbox == 1) 
				{
					_deferred_lighting_mat->DisableKeyword("DEBUG_ALBEDO");
					s_selected_chechbox = -1;
				}
			}

			// 复选框3
			bool checkbox3 = (s_selected_chechbox == 2);
			if (ImGui::Checkbox("DEBUG_WORLDPOS", &checkbox3)) 
			{
				if (checkbox3) 
				{
					_deferred_lighting_mat->EnableKeyword("DEBUG_WORLDPOS");
					//Shader::SetGlobalTexture("SkyBox", g_pRenderer->_p_prefilter_env_tex);
					s_selected_chechbox = 2;
				}
				else if (s_selected_chechbox == 2) {
					_deferred_lighting_mat->DisableKeyword("DEBUG_WORLDPOS");
					s_selected_chechbox = -1;
				}
			}
			//// 获取窗口的尺寸
			//ImVec2 windowSize = ImGui::GetContentRegionAvail();
			//float faceSize = (windowSize.x-25) / 4.0f; // 每个面在窗口中的尺寸
			//ImVec2 tex_size = ImVec2(faceSize, faceSize);
			//auto tex = g_pRenderer->_p_env_tex;
			//auto right = TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetView(0, false, ECubemapFace::kPositiveX));
			//auto left  = TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetView(0, false, ECubemapFace::kNegativeX));
			//auto forward = TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetView(0, false, ECubemapFace::kPositiveZ));
			//auto back = TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetView(0, false, ECubemapFace::kNegativeZ));
			//auto top = TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetView(0, false, ECubemapFace::kPositiveY));
			//auto bottom = TEXTURE_HANDLE_TO_IMGUI_TEXID(tex->GetView(0, false, ECubemapFace::kNegativeY));
			//// 计算UV坐标，以便将cubemap展开显示
			//ImVec2 p = ImGui::GetCursorScreenPos();
			//ImGui::Text("Cubemap Layout: %f,%f",p.x,p.y);
			//ImGui::Dummy(ImVec2(faceSize, 0));
			//ImGui::SameLine();
			//ImGui::Image(right, tex_size); // Right face
			////ImageRotated(forward, ImVec2(p.x + faceSize*0.5,p.y + faceSize*0.5), ImVec2(faceSize, faceSize), 90 * k2Radius);
			////ImGui::Image(forward, ImVec2(faceSize, faceSize), RotatePoint(ImVec2(0, 0),-45 * k2Radius), RotatePoint(ImVec2(1, 1), -45 * k2Radius)); // Front face
			//ImGui::Image(forward, tex_size); // Front face
			//ImGui::SameLine();
			//ImGui::Image(bottom, tex_size); // Bottom face
			//ImGui::SameLine();
			//ImGui::Image(back, tex_size); // Back face
			//ImGui::SameLine();
			//ImGui::Image(top, tex_size); // Top face

			//ImGui::Dummy(ImVec2(faceSize, 0));
			//ImGui::SameLine();
			//ImGui::Image(left, tex_size); // Left face
		}
	}
}