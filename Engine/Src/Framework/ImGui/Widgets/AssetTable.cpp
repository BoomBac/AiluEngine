#include "pch.h"
#include "Framework/ImGui/Widgets/AssetTable.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/ResourceMgr.h"

namespace Ailu
{
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
	void AssetTable::Close()
	{
		ImGuiWidget::Close();
	}
	void AssetTable::ShowImpl()
	{
		// 表头
		ImGui::Text("Path");
		ImGui::SameLine(150);
		ImGui::Text("Type");
		ImGui::SameLine(300);
		ImGui::Text("InstancedObject");

		// 分隔线
		ImGui::Separator();
		for (auto it = g_pResourceMgr->Begin(); it != g_pResourceMgr->End(); it++)
		{
			auto p_asset = it->second;
			ImGui::Text(ToChar(p_asset->_asset_path).c_str());
			ImGui::SameLine(150);
			ImGui::Text(EAssetType::ToString(p_asset->_asset_type));
			ImGui::SameLine(300);
			if (p_asset->_p_inst_asset)
			{
				ImGui::Text("%p", &p_asset->_p_inst_asset);
			}
			else
			{
				ImGui::Text("Null");
			}
			// 分隔线
			ImGui::Separator();
		}
	}
}
