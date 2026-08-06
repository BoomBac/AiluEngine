#include "Automation/AutomationAssetService.h"

#include "Automation/AutomationAdapter.h"
#include "Automation/AutomationReadModel.h"
#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Common/Utils.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            String AssetName(const Asset *asset)
            {
                if (asset->_p_obj && !asset->_p_obj->Name().empty())
                    return asset->_p_obj->Name();
                const String path = ToChar(asset->_asset_path);
                const size_t pos = path.find_last_of("/\\");
                return pos == String::npos ? path : path.substr(pos + 1);
            }

            String AssetTypeName(const Asset *asset)
            {
                if (asset->_asset_type != nullptr)
                    return NormalizeTypeName(asset->_asset_type->FullName());
                return String{};
            }

            const Asset *FindAssetByGuid(const Guid &guid)
            {
                for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                {
                    const Asset *asset = it->second.get();
                    if (asset != nullptr && asset->GetGuid() == guid)
                        return asset;
                }
                return nullptr;
            }
        }// namespace

        void AutomationAssetService::Register(EditorAutomationRegistry &registry)
        {
            {
                AutomationMethodDesc desc;
                desc._name = "asset.query";
                desc._description = "Query project assets by name, type and path prefix.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("name", "Asset name substring filter.", EAutomationValueType::kString);
                desc._input_schema.AddParam("type", "Asset type filter (e.g. Ailu.Render.Material).", EAutomationValueType::kString);
                desc._input_schema.AddParam("path_prefix", "Asset path prefix filter.", EAutomationValueType::kString);
                desc._input_schema.AddParam("limit", "Maximum number of items.", EAutomationValueType::kInteger);
                registry.Register("asset.query", std::move(desc), HandleQuery);
            }
            {
                AutomationMethodDesc desc;
                desc._name = "asset.inspect";
                desc._description = "Inspect an asset by guid or path: metadata and reflected properties.";
                desc._permission = EAutomationPermission::kReadOnly;
                desc._input_schema.AddParam("asset_guid", "Persistent guid of the asset.", EAutomationValueType::kGuid);
                desc._input_schema.AddParam("asset_path", "Asset path (e.g. Assets/Materials/M.alasset).", EAutomationValueType::kString);
                registry.Register("asset.inspect", std::move(desc), HandleInspect);
            }
        }

        AutomationResult AutomationAssetService::HandleQuery(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const String name_filter = arguments.contains("name") ? arguments.at("name").AsString() : String{};
            const String type_filter = arguments.contains("type") ? arguments.at("type").AsString() : String{};
            const String path_prefix = arguments.contains("path_prefix") ? arguments.at("path_prefix").AsString() : String{};
            const i64 limit = arguments.contains("limit") ? arguments.at("limit").AsInt() : 20;

            AutomationArray items;
            bool has_more = false;
            i64 count = 0;

            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                const Asset *asset = it->second.get();
                if (asset == nullptr)
                    continue;

                if (!name_filter.empty() && AssetName(asset).find(name_filter) == String::npos)
                    continue;
                if (!type_filter.empty() && AssetTypeName(asset) != type_filter)
                    continue;
                if (!path_prefix.empty())
                {
                    const String path = ToChar(asset->_asset_path);
                    if (path.rfind(path_prefix, 0) != 0)
                        continue;
                }

                if (static_cast<i64>(items.size()) >= limit)
                {
                    has_more = true;
                    break;
                }

                AutomationObject item;
                item.emplace("asset_guid", AutomationValue(asset->GetGuid().ToString()));
                item.emplace("name", AutomationValue(AssetName(asset)));
                item.emplace("type", AutomationValue(AssetTypeName(asset)));
                item.emplace("path", AutomationValue(ToChar(asset->_asset_path)));
                items.emplace_back(AutomationValue(std::move(item)));
                ++count;
            }

            AutomationObject result;
            result.emplace("items", AutomationValue(std::move(items)));
            result.emplace("has_more", AutomationValue(has_more));
            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }

        AutomationResult AutomationAssetService::HandleInspect(EditorAutomationContext &, const AutomationObject &arguments)
        {
            const Asset *asset = nullptr;

            if (const auto it = arguments.find("asset_guid"); it != arguments.end())
            {
                const Guid guid(it->second.AsString());
                if (!guid.IsValid() || guid.IsEmpty())
                    return AutomationResult::Fail(AutomationErrors::kInvalidGuid, "asset_guid is missing or invalid");
                asset = FindAssetByGuid(guid);
                if (asset == nullptr)
                    return AutomationResult::Fail(AutomationErrors::kAssetNotFound, "asset not found");
            }
            else if (const auto it = arguments.find("asset_path"); it != arguments.end())
            {
                asset = ResourceMgr::Get().GetAsset(ToWChar(it->second.AsString()));
                if (asset == nullptr)
                    return AutomationResult::Fail(AutomationErrors::kAssetNotFound, "asset not found");
            }
            else
            {
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "asset_guid or asset_path is required");
            }

            AutomationObject result;
            result.emplace("asset_guid", AutomationValue(asset->GetGuid().ToString()));
            result.emplace("name", AutomationValue(AssetName(asset)));
            result.emplace("type", AutomationValue(AssetTypeName(asset)));
            result.emplace("path", AutomationValue(ToChar(asset->_asset_path)));

            const String type_name = AssetTypeName(asset);
            if (asset->_p_obj != nullptr)
            {
                const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve(type_name);
                if (adapter != nullptr)
                {
                    AutomationObject properties;
                    for (const String &path : AutomationReadablePaths(adapter, type_name))
                    {
                        AutomationResult value = adapter->ReadProperty(type_name, asset->_p_obj.get(), path);
                        if (value._success)
                            properties.emplace(path, std::move(value._data));
                    }
                    result.emplace("properties", AutomationValue(std::move(properties)));
                }
            }

            return AutomationResult::Ok(AutomationValue(std::move(result)));
        }
    }// namespace Editor
}// namespace Ailu
