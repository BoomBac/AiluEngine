#include "UI/Menu.h"

#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "Framework/Common/KeyCode.h"

#include <memory>

namespace Ailu::UI
{
    namespace
    {
        struct MenuState
        {
            u64 _group_id = 0u;
            MenuOptions _options;
            Vector<Widget *> _popup_widgets;
        };

        u64 s_active_menu_group = 0u;

        void CloseDeeperMenus(const std::shared_ptr<MenuState> &state, u32 level)
        {
            UIManager *manager = UIManager::Get();
            if (manager == nullptr)
                return;

            while (state->_popup_widgets.size() > level + 1u)
            {
                if (manager->GetPopupWidget() == state->_popup_widgets.back())
                    manager->HidePopup();
                state->_popup_widgets.pop_back();
            }
        }

        Ref<UIElement> BuildMenuLevel(const std::shared_ptr<MenuState> &state, const Vector<MenuEntry> &entries,
                                      u32 level, UIElement *parent_item);

        void OpenSubmenu(const std::shared_ptr<MenuState> &state, u32 level, UIElement *parent_item,
                         const Vector<MenuEntry> &entries)
        {
            if (parent_item == nullptr || entries.empty())
                return;

            CloseDeeperMenus(state, level);
            UIManager *manager = UIManager::Get();
            if (manager == nullptr)
                return;

            const Vector4f item_rect = parent_item->GetArrangeRect();
            auto submenu = BuildMenuLevel(state, entries, level + 1u, parent_item);
            if (submenu == nullptr)
                return;

            const f32 submenu_x = item_rect.x + item_rect.z + state->_options._submenu_offset;
            manager->ShowPopupAt(submenu_x, item_rect.y, submenu, nullptr, nullptr, false, true, state->_group_id);
            state->_popup_widgets.push_back(manager->GetPopupWidget());
            submenu->RequestFocus();
        }

        Ref<UIElement> BuildMenuLevel(const std::shared_ptr<MenuState> &state, const Vector<MenuEntry> &entries,
                                      u32 level, UIElement *parent_item)
        {
            if (entries.empty())
                return nullptr;

            const f32 row_height = state->_options._item_height;
            const f32 menu_height = std::min(row_height * static_cast<f32>(entries.size()), state->_options._max_height);
            auto list_view = MakeRef<ListView>();
            list_view->Name(std::format("Menu_{}", level));
            list_view->SetStyleId("Popup");
            UIBrush transparent_brush;
            transparent_brush._type = EUIBrushType::kColor;
            transparent_brush._tint = Colors::kTransparent;
            list_view->SetBackgroundBrush(transparent_brush);
            list_view->GetSlot()->Size({state->_options._item_width, menu_height});
            list_view->SetViewportHeight(menu_height);
            list_view->OnKeyDown() += [state](UIEvent &event)
            {
                if (event._key_code != EKey::kESCAPE)
                    return;
                if (UIManager *manager = UIManager::Get(); manager != nullptr)
                    manager->HidePopupGroup(state->_group_id);
                event._is_handled = true;
            };

            if (parent_item != nullptr)
            {
                list_view->OnMouseEnter() += [parent_item](UIEvent &)
                {
                    parent_item->SetHovered(true);
                };
            }

            for (const MenuEntry &entry: entries)
            {
                auto item = MakeRef<HorizontalBox>();
                item->SlotPadding() = Padding(6.0f, 3.0f, 6.0f, 3.0f);

                auto *label = item->AddChild<Text>(entry._is_separator ? "" : entry._label);
                label->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
                label->_horizontal_align = EAlignment::kLeft;
                label->_vertical_align = EAlignment::kCenter;
                if (entry._is_destructive)
                    label->_color = Colors::kRed;
                else if (!entry._is_enabled || entry._is_separator)
                    label->_color = Colors::kGray;

                if (!entry._children.empty())
                {
                    auto *arrow = item->AddChild<Text>(">");
                    arrow->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFill)
                        .Size({16.0f, row_height});
                    arrow->_horizontal_align = EAlignment::kCenter;
                    arrow->_vertical_align = EAlignment::kCenter;
                    if (!entry._is_enabled)
                        arrow->_color = Colors::kGray;
                }

                const bool has_children = !entry._children.empty();
                const bool is_enabled = entry._is_enabled && !entry._is_separator;
                const Vector<MenuEntry> children = entry._children;
                item->OnMouseEnter() += [state, level, item_ptr = item.get(), has_children, is_enabled, children](UIEvent &)
                {
                    if (!is_enabled)
                        return;
                    if (has_children)
                        OpenSubmenu(state, level, item_ptr, children);
                    else
                        CloseDeeperMenus(state, level);
                };
                item->OnMouseClick() += [state, entry, is_enabled](UIEvent &event)
                {
                    if (!is_enabled || !entry._children.empty())
                        return;
                    if (UIManager *manager = UIManager::Get(); manager != nullptr)
                        manager->HidePopupGroup(state->_group_id);
                    if (entry._on_click)
                        entry._on_click();
                    event._is_handled = true;
                };
                list_view->AddItem(item);
                item->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed)
                    .Size({state->_options._item_width, row_height});
            }

            return list_view;
        }
    }

    void Menu::ShowAt(Vector2f position, const Vector<MenuEntry> &entries, const MenuOptions &options)
    {
        if (entries.empty() || UIManager::Get() == nullptr)
            return;

        UIManager *manager = UIManager::Get();
        if (s_active_menu_group != 0u)
            manager->HidePopupGroup(s_active_menu_group);
        if (Widget *popup = manager->GetPopupWidget(); popup != nullptr && !manager->IsPopupModal(popup))
            manager->HidePopup();

        auto state = std::make_shared<MenuState>();
        state->_group_id = manager->CreatePopupGroup();
        state->_options = options;
        auto root = BuildMenuLevel(state, entries, 0u, nullptr);
        if (root == nullptr)
            return;

        manager->ShowPopupAt(position.x, position.y, root, nullptr, nullptr, false, true, state->_group_id);
        state->_popup_widgets.push_back(manager->GetPopupWidget());
        root->RequestFocus();
        s_active_menu_group = state->_group_id;
    }

    void Menu::ShowAt(UIElement *anchor, const Vector<MenuEntry> &entries, const MenuOptions &options)
    {
        if (anchor == nullptr)
            return;
        const Vector4f rect = anchor->GetArrangeRect();
        ShowAt({rect.x, rect.y + rect.w}, entries, options);
    }

    void Menu::Hide()
    {
        if (s_active_menu_group != 0u && UIManager::Get() != nullptr)
            UIManager::Get()->HidePopupGroup(s_active_menu_group);
        s_active_menu_group = 0u;
    }
}
