#pragma once

#include "UI/Container.h"
#include "Framework/Math/Guid.h"

namespace Ailu
{
    class Asset;
    class Object;
    class Type;

    namespace Render
    {
        class Texture;
    }

    namespace UI
    {
        class AILU_API ObjectAssetDropdown final : public Dropdown
        {
            DECLARE_DELEGATE(on_object_asset_selected, Asset *, Object *, const Guid &);

        public:
            using AssetIconProvider = std::function<Render::Texture *(Asset *)>;

            explicit ObjectAssetDropdown(const Type *object_type = nullptr);

            static void SetAssetIconProvider(AssetIconProvider provider);

            void SetObjectType(const Type *object_type);
            const Type *GetObjectType() const { return _object_type; }
            void SetAllowNone(bool allow_none);
            bool AllowsNone() const { return _allow_none; }
            void SetSelectedGuid(const Guid &guid, bool notify = false);
            const Guid &GetSelectedGuid() const { return _selected_guid; }
            Asset *GetSelectedAsset() const;
            Object *GetSelectedObject() const;
            void RefreshItems();

        private:
            struct Entry
            {
                Asset *_asset = nullptr;
                Object *_object = nullptr;
                Guid _guid = Guid::EmptyGuid();
                String _name;
            };

            static bool IsTypeCompatible(const Type *requested_type, const Type *actual_type);
            void SelectEntry(i32 index, bool notify);
            i32 FindEntryIndex(const Guid &guid) const;
            Ref<UIElement> BuildPopup(Vector2f anchor_size);
            void PopulateListView(ListView *list_view, const String &search_text);
            Ref<UIElement> BuildPopupItem(i32 index);
            Render::Texture *GetPreviewTexture(Entry &entry) const;

        private:
            const Type *_object_type = nullptr;
            Vector<Entry> _entries;
            Guid _selected_guid = Guid::EmptyGuid();
            bool _allow_none = true;
            bool _is_syncing_selection = false;
            inline static AssetIconProvider s_asset_icon_provider;
        };
    }// namespace UI
}// namespace Ailu
