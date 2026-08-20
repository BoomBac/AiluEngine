#include "UI/ObjectAssetDropdown.h"

#include "Assets/Asset.h"
#include "Framework/Common/ResourceMgr.h"
#include "Objects/Type.h"
#include "Render/2D/Sprite.h"
#include "Render/Texture.h"
#include "UI/Basic.h"
#include "UI/UIFramework.h"

#include <algorithm>
#include <cctype>

namespace Ailu
{
    namespace UI
    {
        namespace
        {
            constexpr f32 kPreviewSize = 36.0f;
            constexpr f32 kItemHeight = 40.0f;
        }

        ObjectAssetDropdown::ObjectAssetDropdown(const Type *object_type) : _object_type(object_type)
        {
            Name("ObjectAssetDropdown");
            SetPopupBuilder([this](Vector2f anchor_size) { return BuildPopup(anchor_size); });
            SetOnPopupOpening([this]() { RefreshItems(); });
            _on_selected_changed += [this](i32 index) { SelectEntry(index, !_is_syncing_selection); };
            RefreshItems();
        }

        void ObjectAssetDropdown::SetAssetIconProvider(AssetIconProvider provider)
        {
            s_asset_icon_provider = std::move(provider);
        }

        void ObjectAssetDropdown::SetObjectType(const Type *object_type)
        {
            if (_object_type == object_type)
                return;
            _object_type = object_type;
            RefreshItems();
        }

        void ObjectAssetDropdown::SetAllowNone(bool allow_none)
        {
            if (_allow_none == allow_none)
                return;
            _allow_none = allow_none;
            RefreshItems();
        }

        void ObjectAssetDropdown::SetSelectedGuid(const Guid &guid, bool notify)
        {
            _selected_guid = guid;
            const i32 index = FindEntryIndex(guid);
            _is_syncing_selection = !notify;
            Dropdown::SetSelectedIndex(index);
            _is_syncing_selection = false;
            if (notify && index < 0)
                _on_object_asset_selected_delegate.Invoke(nullptr, nullptr, Guid::EmptyGuid());
        }

        Asset *ObjectAssetDropdown::GetSelectedAsset() const
        {
            const i32 index = FindEntryIndex(_selected_guid) - (_allow_none ? 1 : 0);
            return index < 0 || index >= static_cast<i32>(_entries.size()) ? nullptr : _entries[index]._asset;
        }

        Object *ObjectAssetDropdown::GetSelectedObject() const
        {
            const i32 index = FindEntryIndex(_selected_guid) - (_allow_none ? 1 : 0);
            return index < 0 || index >= static_cast<i32>(_entries.size()) ? nullptr : _entries[index]._object;
        }

        void ObjectAssetDropdown::RefreshItems()
        {
            _entries.clear();
            Vector<String> names;
            if (_allow_none)
                names.emplace_back("None");

            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                if (asset == nullptr || !IsTypeCompatible(_object_type, asset->_asset_type))
                    continue;
                Entry entry;
                entry._asset = asset;
                entry._object = asset->_p_obj.get();
                entry._guid = asset->GetGuid();
                entry._name = asset->Name();
                _entries.emplace_back(std::move(entry));
                names.emplace_back(_entries.back()._name);
            }

            for (const ResourceMgr::SubAssetEntry &sub_asset : ResourceMgr::Get().GetSubAssets(_object_type))
            {
                if (FindEntryIndex(sub_asset._guid) >= 0)
                    continue;
                Entry entry;
                entry._asset = ResourceMgr::Get().GetAsset(ResourceMgr::Get().GuidToAssetPath(sub_asset._guid));
                entry._object = ResourceMgr::Get().GetRef<Object>(sub_asset._guid).get();
                entry._guid = sub_asset._guid;
                entry._name = sub_asset._name;
                _entries.emplace_back(std::move(entry));
                names.emplace_back(_entries.back()._name);
            }

            std::sort(_entries.begin(), _entries.end(), [](const Entry &lhs, const Entry &rhs) { return lhs._name < rhs._name; });
            if (_allow_none)
            {
                names.resize(1u);
                for (const Entry &entry : _entries)
                    names.emplace_back(entry._name);
            }
            else
            {
                names.clear();
                for (const Entry &entry : _entries)
                    names.emplace_back(entry._name);
            }
            SetItems(names);
            _is_syncing_selection = true;
            Dropdown::SetSelectedIndex(FindEntryIndex(_selected_guid));
            _is_syncing_selection = false;
        }

        bool ObjectAssetDropdown::IsTypeCompatible(const Type *requested_type, const Type *actual_type)
        {
            if (requested_type == nullptr)
                return true;
            for (const Type *type = actual_type; type != nullptr; type = type->BaseType())
            {
                if (type == requested_type)
                    return true;
            }
            return false;
        }

        void ObjectAssetDropdown::SelectEntry(i32 index, bool notify)
        {
            if (_allow_none && index == 0)
            {
                _selected_guid = Guid::EmptyGuid();
                if (notify)
                    _on_object_asset_selected_delegate.Invoke(nullptr, nullptr, _selected_guid);
                return;
            }
            const i32 entry_index = index - (_allow_none ? 1 : 0);
            if (entry_index < 0 || entry_index >= static_cast<i32>(_entries.size()))
                return;

            Entry &entry = _entries[entry_index];
            if (entry._object == nullptr && entry._asset != nullptr)
                entry._object = ResourceMgr::Get().Load(entry._asset->_asset_path, nullptr, entry._asset->_asset_type).get();
            if (entry._object == nullptr && !entry._guid.IsEmpty())
                entry._object = ResourceMgr::Get().Load<Object>(entry._guid).get();
            _selected_guid = entry._guid;
            if (notify)
                _on_object_asset_selected_delegate.Invoke(entry._asset, entry._object, _selected_guid);
        }

        i32 ObjectAssetDropdown::FindEntryIndex(const Guid &guid) const
        {
            if (guid.IsEmpty())
                return _allow_none ? 0 : -1;
            for (i32 index = 0; index < static_cast<i32>(_entries.size()); ++index)
            {
                if (_entries[index]._guid == guid)
                    return index + (_allow_none ? 1 : 0);
            }
            return -1;
        }

        Ref<UIElement> ObjectAssetDropdown::BuildPopup(Vector2f anchor_size)
        {
            constexpr f32 kPopupHeight = 450.0f;
            constexpr f32 kSearchHeight = 32.0f;
            constexpr f32 kPopupPadding = 6.0f;
            auto popup = MakeRef<Border>();
            popup->Name("ObjectAssetDropdownPopup");
            popup->GetSlot()->Size({std::max(anchor_size.x, 280.0f), kPopupHeight});
            UIBrush backdrop;
            backdrop._type = EUIBrushType::kBackdropBlur;
            backdrop._tint = Color(0.16f, 0.18f, 0.21f, 0.46f);
            auto &popup_style = popup->GetStyleOverride();
            popup_style.SetBackground(backdrop);
            popup_style.SetBorderColor(Color(0.28f, 0.31f, 0.35f, 1.0f));
            popup_style.SetBorderWidth(1.0f);
            popup_style.SetCornerRadius(10.0f);
            popup->SlotPadding() = Padding(kPopupPadding);

            auto *content = popup->AddChild<VerticalBox>();
            content->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            auto *search = content->AddChild<InputBlock>();
            search->Name("ObjectAssetDropdownSearch");
            search->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed)
                .Size({0.0f, kSearchHeight}).Margin({0.0f, 0.0f, 0.0f, kPopupPadding});
            search->SetContent("");

            auto *list_view = content->AddChild<ListView>();
            list_view->Name("ObjectAssetDropdownList");
            list_view->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            UIBrush transparent_brush;
            transparent_brush._type = EUIBrushType::kColor;
            transparent_brush._tint = Colors::kTransparent;
            list_view->SetBackgroundBrush(transparent_brush);
            list_view->SetStyleId("DropdownPopup");
            PopulateListView(list_view, "");
            search->_on_content_changed += [this, list_view](String content) { PopulateListView(list_view, content); };
            return popup;
        }

        void ObjectAssetDropdown::PopulateListView(ListView *list_view, const String &search_text)
        {
            if (list_view == nullptr)
                return;
            String lowered_search = search_text;
            std::transform(lowered_search.begin(), lowered_search.end(), lowered_search.begin(), [](unsigned char ch)
            { return static_cast<char>(std::tolower(ch)); });
            list_view->ClearItems();
            if (_allow_none && lowered_search.empty())
            {
                auto none_item = BuildPopupItem(0);
                none_item->OnMouseClick() += [this](UIEvent &)
                {
                    SetSelectedIndex(0);
                    UIManager::Get()->HidePopup();
                };
                list_view->AddItem(none_item);
            }
            for (i32 entry_index = 0; entry_index < static_cast<i32>(_entries.size()); ++entry_index)
            {
                String lowered_name = _entries[entry_index]._name;
                std::transform(lowered_name.begin(), lowered_name.end(), lowered_name.begin(), [](unsigned char ch)
                { return static_cast<char>(std::tolower(ch)); });
                if (!lowered_search.empty() && lowered_name.find(lowered_search) == String::npos)
                    continue;
                const i32 dropdown_index = entry_index + (_allow_none ? 1 : 0);
                auto item = BuildPopupItem(dropdown_index);
                item->OnMouseClick() += [this, dropdown_index](UIEvent &)
                {
                    SetSelectedIndex(dropdown_index);
                    UIManager::Get()->HidePopup();
                };
                list_view->AddItem(item);
            }
        }

        Ref<UIElement> ObjectAssetDropdown::BuildPopupItem(i32 index)
        {
            auto row = MakeRef<HorizontalBox>();
            row->GetSlot()->Size({0.0f, kItemHeight});
            if (_allow_none && index == 0)
            {
                row->AddChild<Text>("None")->GetSlotAs<LinearSlot>()
                    .SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size({0.0f, kItemHeight}).Margin(Padding(6.0f));
                return row;
            }

            const i32 entry_index = index - (_allow_none ? 1 : 0);
            if (entry_index < 0 || entry_index >= static_cast<i32>(_entries.size()))
                return row;
            Entry &entry = _entries[entry_index];
            if (Render::Texture *preview = GetPreviewTexture(entry); preview != nullptr)
            {
                auto *image = row->AddChild<Image>(preview);
                image->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed)
                    .Size({kPreviewSize, kPreviewSize}).Margin({4.0f, 2.0f, 6.0f, 2.0f});
            }
            auto *name = row->AddChild<Text>(entry._name);
            name->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed)
                .Size({0.0f, kItemHeight}).Margin(Padding(2.0f));
            name->_vertical_align = EAlignment::kCenter;
            return row;
        }

        Render::Texture *ObjectAssetDropdown::GetPreviewTexture(Entry &entry) const
        {
            if (entry._object == nullptr && entry._asset != nullptr)
                entry._object = entry._asset->_p_obj.get();
            if (auto *sprite = dynamic_cast<Render::Sprite *>(entry._object); sprite != nullptr)
                return sprite->_texture.get();
            if (auto *preview = dynamic_cast<Render::Texture *>(entry._object); preview != nullptr)
                return preview;
            return s_asset_icon_provider && entry._asset != nullptr ? s_asset_icon_provider(entry._asset) : nullptr;
        }
    }// namespace UI
}// namespace Ailu
