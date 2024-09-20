#include "Widgets/AssetTable.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/GraphicsContext.h"

namespace Ailu
{
	namespace Editor
	{
		static void PushStyleCompact()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, (float)(int)(style.FramePadding.y * 0.60f)));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, (float)(int)(style.ItemSpacing.y * 0.60f)));
		}

		static void PopStyleCompact()
		{
			ImGui::PopStyleVar(2);
		}

		AssetTable::AssetTable() : ImGuiWidget("AssetTable")
		{
		}
		AssetTable::~AssetTable()
		{
		}
		void AssetTable::Open(const i32& handle)
		{
			ImGuiWidget::Open(handle);
		}
		void AssetTable::Close(i32 handle)
		{
			ImGuiWidget::Close(handle);
		}
		void AssetTable::ShowImpl()
		{
			static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV;
//			PushStyleCompact();
//			ImGui::CheckboxFlags("ImGuiTableFlags_Resizable", &flags, ImGuiTableFlags_Resizable);
//			ImGui::CheckboxFlags("ImGuiTableFlags_Reorderable", &flags, ImGuiTableFlags_Reorderable);
//			ImGui::CheckboxFlags("ImGuiTableFlags_Hideable", &flags, ImGuiTableFlags_Hideable);
//			ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBody", &flags, ImGuiTableFlags_NoBordersInBody);
//			ImGui::CheckboxFlags("ImGuiTableFlags_NoBordersInBodyUntilResize", &flags, ImGuiTableFlags_NoBordersInBodyUntilResize);
//			PopStyleCompact();
			ImGui::Spacing();
			if (ImGui::TreeNode("AssetTableInfo"))
			{
				ImGui::Text("Total Asset Num: %d", g_pResourceMgr->AssetNum());
				if (ImGui::BeginTable("AssetTable", 5, flags))
				{
					// Submit columns name with TableSetupColumn() and call TableHeadersRow() to create a row with a header in each column.
					// (Later we will show how TableSetupColumn() has other uses, optional flags, sizing weight etc.)
					ImGui::TableSetupColumn("Name");
					ImGui::TableSetupColumn("Path");
					ImGui::TableSetupColumn("Type");
					ImGui::TableSetupColumn("Object");
					ImGui::TableSetupColumn("RefCount");
					ImGui::TableHeadersRow();
					for (auto it = g_pResourceMgr->Begin(); it != g_pResourceMgr->End(); it++)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						auto p_asset = it->second.get();
						if (!p_asset)
						{
							ImGui::Text("?????????");
							continue;
						}
						else
						{
							ImGui::Text(ToChar(p_asset->_name).c_str());
						}
						ImGui::TableSetColumnIndex(1);
						ImGui::Text(ToChar(p_asset->_asset_path).c_str());
						ImGui::TableSetColumnIndex(2);
						ImGui::Text(EAssetType::ToString(p_asset->_asset_type));
						ImGui::TableSetColumnIndex(3);
						if (p_asset->_p_obj)
                            ImGui::Text("%p", &p_asset->_p_obj);
						else
                            ImGui::Text("Null");
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%d", std::max<u32>(p_asset->_p_obj.use_count() - 1,0));
					}
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("RenderTextureInfo"))
			{
				ImGui::Text("Total GPU Memery Size: %.2f mb", (f32)RenderTexture::TotalGPUMemerySize() * 9.5367431640625E-07);
				ImGui::Text("Total RT Num: %d", g_pRenderTexturePool->Size());
				if (ImGui::Button("Try Relsease"))
				{
					g_pGfxContext->TryReleaseUnusedResources();
				}
				if (ImGui::BeginTable("RenderTexture", 7, flags))
				{
					// Submit columns name with TableSetupColumn() and call TableHeadersRow() to create a row with a header in each column.
					// (Later we will show how TableSetupColumn() has other uses, optional flags, sizing weight etc.)
					ImGui::TableSetupColumn("Name");
					ImGui::TableSetupColumn("MipmapLevel");
					ImGui::TableSetupColumn("Size");
					ImGui::TableSetupColumn("Format");
					ImGui::TableSetupColumn("MemorySize");
					ImGui::TableSetupColumn("FenceValue");
					ImGui::TableSetupColumn("LastAccessFrameIndex");
					ImGui::TableHeadersRow();
					u32 row = 0;
					for (auto& it : *g_pRenderTexturePool)
					{
						ImGui::TableNextRow();
						auto& [rt_hash, rt_info] = it;
						ImGui::TableSetColumnIndex(0);
						ImGui::Text(rt_info._rt->Name().c_str());
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%d", rt_info._rt->MipmapLevel() + 1);
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%d x %d", rt_info._rt->Width(), rt_info._rt->Height());
						ImGui::TableSetColumnIndex(3);
						ImGui::Text(EALGFormat::ToString(rt_info._rt->PixelFormat()));
						ImGui::TableSetColumnIndex(4);
						//byte to mb
						ImGui::Text("%.2f mb", (f32)rt_info._rt->GetGpuMemerySize() * 9.5367431640625E-07);
						ImGui::TableSetColumnIndex(5);
						ImGui::Text("%d", rt_info._rt->GetFenceValue());
						ImGui::TableSetColumnIndex(6);
						ImGui::Text("%d", rt_info._last_access_frame_count);
						row++;
					}
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
		}
	}
}
