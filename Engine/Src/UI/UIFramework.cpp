//
// Created by 22292 on 2024/10/28.
//
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "UI/UILayer.h"
#include "UI/Widget.h"
#include "UI/Container.h"
#include "UI/Basic.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Allocator.hpp"

namespace Ailu::UI
{
    static UIManager* g_pUIManager = nullptr;
    //----------------------------------------------------------------------------------------UIManager-----------------------------------------------------------------------------
    void UIManager::Init()
    {
        g_pUIManager = AL_NEW_TAG(EMemoryTag::kUi, UIManager);
    }
    void UIManager::Shutdown()
    {
        UIRenderer::Shutdown();
        AL_DELETE(g_pUIManager);
    }
    UIManager *UIManager::Get()
    {
        return g_pUIManager;
    }

    UIManager::UIManager()
    {
        UIRenderer::Init();
        _ui_layer = AL_NEW_TAG(EMemoryTag::kUi, UILayer);
        Application::Get().PushLayer(_ui_layer);
        _renderer = UIRenderer::Get();
        _capture_target = nullptr;
        _debug_highlight_target = nullptr;
    }
    UIManager::~UIManager()
    {
        for (auto& it : _widgets)
        {
            it->Destory();
        }
        _widgets.clear();
    }

    void UIManager::Update(f32 dt)
    {
        for (auto &widget: _pending_popup_destroy)
        {
            if (widget)
                widget->Destory();
            widget.reset();
        }
        _pending_popup_destroy.clear();

        // 处理待销毁的元素
        for (auto &e: _pending_destroy)
        {
            e.reset();
        }
        _pending_destroy.clear();
    }

    void UIManager::RegisterWidget(Ref<Widget> w)
    {

        if (auto it = std::find_if(_widgets.begin(), _widgets.end(), [&](Ref<Widget> e) -> bool
                                   { return e.get() == w.get(); });
            it != _widgets.end())
            return;
        _widgets.push_back(w);
    }

    void UIManager::UnRegisterWidget(Widget *w)
    {
        CleanupWidgetState(w);
        std::erase_if(_widgets, [&](Ref<Widget> e) -> bool
                      { return e.get() == w; });
    }

    void UIManager::BringToFront(Widget *w)
    {
        auto it = std::find_if(_widgets.begin(), _widgets.end(), [&](Ref<Widget> e) -> bool
                               { return e.get() == w; });
        if (it == _widgets.end())
            return;
        Ref<Widget> current = *it;
        if (_widgets.size() > 1 && _widgets.back().get() != w)
            _widgets.back()->_on_lost_focus_delegate.Invoke();
        if (it == _widgets.end() - 1)
        {
            w->_on_get_focus_delegate.Invoke();
            return;//already in front
        }
        if (it != _widgets.end())
        {
            _widgets.erase(it);
            _widgets.push_back(current);// 移到最前
        }
        // 重新计算z
        for (u64 i = 0; i < _widgets.size(); i++)
        {
            _widgets[i]->_sort_order = (u32)i;
        }
        w->_on_get_focus_delegate.Invoke();
        //LOG_INFO("{}: BringToFront", GetThreadName());
    }

    void UIManager::BringToFrontSilently(Widget *w)
    {
        auto it = std::find_if(_widgets.begin(), _widgets.end(), [&](const Ref<Widget> &e) -> bool
                               { return e.get() == w; });
        if (it == _widgets.end() || it == _widgets.end() - 1)
            return;
        Ref<Widget> current = *it;
        _widgets.erase(it);
        _widgets.push_back(current);
        for (u64 i = 0; i < _widgets.size(); i++)
            _widgets[i]->_sort_order = (u32) i;
    }

    void UIManager::SendToBack(Widget *w)
    {
        auto it = std::find_if(_widgets.begin(), _widgets.end(), [&](const Ref<Widget> &e) -> bool
                               { return e.get() == w; });
        if (it == _widgets.end() || it == _widgets.begin())
            return;
        Ref<Widget> current = *it;
        _widgets.erase(it);
        _widgets.insert(_widgets.begin(), current);
        for (u64 i = 0; i < _widgets.size(); i++)
            _widgets[i]->_sort_order = (u32) i;
    }

    void UIManager::EnsurePopupWidgetsOnTop()
    {
        if (_popup_stack.empty())
            return;
        for (const auto &popup: _popup_stack)
        {
            if (!popup._widget)
                continue;
            auto it = std::find_if(_widgets.begin(), _widgets.end(), [&](const Ref<Widget> &e) -> bool
                                   { return e.get() == popup._widget.get(); });
            if (it == _widgets.end() || it == _widgets.end() - 1)
                continue;
            Ref<Widget> current = *it;
            _widgets.erase(it);
            _widgets.push_back(current);
        }
        for (u64 i = 0; i < _widgets.size(); i++)
            _widgets[i]->_sort_order = (u32) i;
    }

    void UIManager::SetFocus(UIElement *element)
    {
        if (_focus_target == element)
            return;
        UIElement *old = _focus_target;
        _focus_target = element;
        ApplyFocusChange(old, element);
        LOG_INFO("UIManager::SetFocus: foucs on {}", element ? element->Name() : "null");
    }

    void UIManager::ClearFocus(UIElement *element)
    {
        if (element && _focus_target != element)
            return;// 只清除当前焦点
        if (_focus_target == nullptr)
            return;
        UIElement *old = _focus_target;
        _focus_target = nullptr;
        ApplyFocusChange(old, nullptr);
    }
    void UIManager::ShowPopupAt(f32 x, f32 y, Ref<UIElement> root, std::function<void()> on_close, Window *win,
                                bool is_modal, bool render_backdrop, u64 popup_group_id)
    {
        if (!root)
            return;

        Window *target_window = win ? win : &Application::Get().GetWindow();
        auto popup_widget = MakeRef<Widget>();
        popup_widget->Name(std::format("PopupWidget_{}", _popup_stack.size()));
        popup_widget->SetPopup(true);
        const auto &root_slot = root->GetSlot();
        const auto *root_canvas_slot = dynamic_cast<const CanvasSlot *>(root_slot.get());
        const Vector2f popup_size = root_canvas_slot != nullptr && root_canvas_slot->_size_to_content ?
                                        root->MeasureDesiredSize() : root_slot->_size;

        auto popup_root = MakeRef<Canvas>();
        popup_root->Name(std::format("{}Root", popup_widget->Name()));
        const bool root_has_default_frame = root->As<Border>() != nullptr || root->As<ListView>() != nullptr;
        if (!is_modal && !root_has_default_frame)
        {
            auto popup_frame = MakeRef<Border>();
            popup_frame->Name(std::format("{}Frame", popup_widget->Name()));
            auto &frame_slot = popup_root->AddChild(popup_frame)->GetSlotAs<CanvasSlot>();
            frame_slot.Position(Vector2f::kZero).Size(popup_size);

            UIBrush transparent_brush;
            transparent_brush._type = EUIBrushType::kColor;
            transparent_brush._tint = Colors::kTransparent;
            auto &frame_style = popup_frame->GetStyleOverride();
            frame_style.SetBackground(transparent_brush);
            popup_frame->SetStyleId("Popup");
            if (_theme == nullptr || _theme->FindBorderStyle("Popup") == nullptr)
            {
                frame_style.SetBorderColor(Color(0.28f, 0.31f, 0.35f, 1.0f));
                frame_style.SetBorderWidth(1.0f);
                frame_style.SetCornerRadius(10.0f);
            }

            popup_frame->AddChild(root);
            root->GetSlotAs<LinearSlot>().Margin(Padding(0.0f)).SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
        }
        else
        {
            popup_root->AddChild(root);
            auto &canvas_slot = root->GetSlotAs<CanvasSlot>();
            canvas_slot.Position(Vector2f::kZero).Size(popup_size);
        }

        const Vector2f window_size = {(f32) target_window->GetWidth(), (f32) target_window->GetHeight()};
        constexpr f32 kScreenPadding = 4.0f;
        Vector2f popup_pos{x, y};
        if (popup_pos.x + popup_size.x + kScreenPadding > window_size.x)
            popup_pos.x = x - popup_size.x;
        if (popup_pos.y + popup_size.y + kScreenPadding > window_size.y)
            popup_pos.y = y - popup_size.y;
        popup_pos.x = std::clamp(popup_pos.x, kScreenPadding, std::max(kScreenPadding, window_size.x - popup_size.x - kScreenPadding));
        popup_pos.y = std::clamp(popup_pos.y, kScreenPadding, std::max(kScreenPadding, window_size.y - popup_size.y - kScreenPadding));

        if (is_modal)
        {
            if (auto *border = root->As<Border>())
            {
                if (auto *backdrop_texture = RenderTexture::WindowBackBuffer(target_window))
                {
                    UIBrush backdrop_brush;
                    backdrop_brush._type = EUIBrushType::kBackdropBlur;
                    backdrop_brush._texture = backdrop_texture;
                    backdrop_brush._tint = Color(0.12f, 0.12f, 0.14f, 0.72f);
                    if (_theme != nullptr)
                    {
                        if (const auto *popup_style = _theme->FindBorderStyle("Popup"))
                        {
                            backdrop_brush._tint = popup_style->_visual._background._tint;
                            backdrop_brush._tint.a = 0.72f;
                        }
                    }
                    border->GetStyleOverride().SetBackground(backdrop_brush);
                }
            }
        }

        popup_widget->SetParent(target_window);
        popup_widget->BindOutput(RenderTexture::WindowBackBuffer(target_window));
        popup_widget->SetPosition(popup_pos);
        popup_widget->SetSize(popup_size);
        popup_widget->AddToWidget(popup_root);
        popup_widget->_visibility = EVisibility::kVisible;
        RegisterWidget(popup_widget);
        BringToFront(popup_widget.get());
        _popup_stack.push_back({popup_widget, on_close, is_modal, render_backdrop, popup_group_id});
    }
    void UIManager::HidePopup()
    {
        if (_popup_stack.empty())
            return;

        PopupEntry entry = std::move(_popup_stack.back());
        _popup_stack.pop_back();
        if (entry._widget)
        {
            entry._widget->_visibility = EVisibility::kHide;
            if (entry._widget->Root() && !entry._widget->Root()->GetChildren().empty())
                LOG_INFO("UIManager::HidePopup: destory element {}", entry._widget->Root()->ChildAt(0u)->Name())
            UnRegisterWidget(entry._widget.get());
            if (_pre_hover_widget == entry._widget.get())
                _pre_hover_widget = nullptr;
            if (_capture_target != nullptr && entry._widget->Root() != nullptr)
            {
                UIElement *node = _capture_target;
                while (node != nullptr)
                {
                    if (node == entry._widget->Root())
                    {
                        _capture_target = nullptr;
                        break;
                    }
                    node = node->GetParent();
                }
            }
            _pending_popup_destroy.push_back(entry._widget);
        }
        if (entry._on_close)
            entry._on_close();
    }
    void UIManager::HidePopupGroup(u64 popup_group_id)
    {
        if (popup_group_id == 0u)
            return;

        while (true)
        {
            auto it = std::find_if(_popup_stack.rbegin(), _popup_stack.rend(),
                                   [popup_group_id](const PopupEntry &entry)
                                   { return entry._group_id == popup_group_id; });
            if (it == _popup_stack.rend())
                break;

            const size_t index = static_cast<size_t>(std::distance(_popup_stack.begin(), it.base()) - 1);
            if (index == _popup_stack.size() - 1u)
            {
                HidePopup();
                continue;
            }

            PopupEntry entry = std::move(_popup_stack[index]);
            _popup_stack.erase(_popup_stack.begin() + static_cast<std::ptrdiff_t>(index));
            if (entry._widget)
            {
                entry._widget->_visibility = EVisibility::kHide;
                UnRegisterWidget(entry._widget.get());
                _pending_popup_destroy.push_back(entry._widget);
            }
            if (entry._on_close)
                entry._on_close();
        }
    }
    Widget *UIManager::GetPopupWidget() const
    {
        if (_popup_stack.empty())
            return nullptr;
        return _popup_stack.back()._widget.get();
    }
    Widget *UIManager::GetModalPopupWidget() const
    {
        for (auto it = _popup_stack.rbegin(); it != _popup_stack.rend(); ++it)
        {
            if (it->_is_modal)
                return it->_widget.get();
        }
        return nullptr;
    }
    u64 UIManager::GetPopupGroupId(const Widget *widget) const
    {
        if (widget == nullptr)
            return 0u;
        for (auto it = _popup_stack.rbegin(); it != _popup_stack.rend(); ++it)
        {
            if (it->_widget.get() == widget)
                return it->_group_id;
        }
        return 0u;
    }
    bool UIManager::IsPointInsidePopupGroup(u64 popup_group_id, Vector2f position) const
    {
        if (popup_group_id == 0u)
            return false;
        for (const auto &entry: _popup_stack)
        {
            if (entry._group_id == popup_group_id && entry._widget != nullptr && entry._widget->IsHover(position))
                return true;
        }
        return false;
    }
    bool UIManager::IsPopupModal(const Widget *widget) const
    {
        if (widget == nullptr)
            return false;
        for (auto it = _popup_stack.rbegin(); it != _popup_stack.rend(); ++it)
        {
            if (it->_widget.get() == widget)
                return it->_is_modal;
        }
        return false;
    }
    bool UIManager::IsPopupAboveModal(const Widget *widget) const
    {
        if (widget == nullptr)
            return false;
        for (auto it = _popup_stack.rbegin(); it != _popup_stack.rend(); ++it)
        {
            if (it->_widget.get() == widget)
                return true;
            if (it->_is_modal)
                return false;
        }
        return false;
    }
    bool UIManager::ShouldRenderPopupBackdrop(const Widget *widget) const
    {
        if (widget == nullptr)
            return false;
        for (auto it = _popup_stack.rbegin(); it != _popup_stack.rend(); ++it)
        {
            if (it->_widget.get() == widget)
                return it->_render_backdrop;
        }
        return true;
    }
    void UIManager::SetTheme(UITheme *theme) { _theme = theme; }
    void UIManager::Destroy(Ref<UIElement> element)
    {
        if (element)
        {
            _pending_destroy.push_back(element);
        }
    }
    void UIManager::OnElementDestroying(UIElement *element)
    {
        if (_capture_target == element)
            _capture_target = nullptr;
        if (_focus_target == element)
            _focus_target = nullptr;
        if (_hover_target == element)
            _hover_target = nullptr;
        if (_debug_highlight_target == element)
            _debug_highlight_target = nullptr;
        for (auto &w: _widgets)
        {
            std::erase(w->_prev_hover_path, element);
            if (w->_last_click_target == element)
                w->ResetClickState();
        }
    }
    ZoneHandle UIManager::RegisterInteractionZone(Vector4f rect, Widget *owner)
    {
        u32 index;
        if (!_free_indices.empty())
        {
            index = _free_indices.back();
            _free_indices.pop_back();
            _interaction_zones[index]._rect = rect;
            _interaction_zones[index]._owner = owner;
        }
        else
        {
            index = (u32)_interaction_zones.size();
            _interaction_zones.push_back({rect, 0, owner});
        }

        return {index, _interaction_zones[index]._generation};
    }
    void UIManager::UnRegisterInteractionZone(ZoneHandle h)
    {
        if (h._index >= _interaction_zones.size())
            return;

        if (_interaction_zones[h._index]._generation != h._generation)
            return;// stale handle

        _interaction_zones[h._index]._generation++;
        _free_indices.push_back(h._index);
    }
    void UIManager::UpdateInteractionZone(ZoneHandle h, Vector4f rect)
    {
        if (h._index < _interaction_zones.size() &&
            _interaction_zones[h._index]._generation == h._generation)
        {
            _interaction_zones[h._index]._rect = rect;
        }
    }
    void UIManager::ApplyFocusChange(UIElement *old_f, UIElement *new_f)
    {
        if (old_f)
            old_f->SetFocusedInternal(false);
        if (new_f)
            new_f->SetFocusedInternal(true);
    }

    bool UIManager::IsElementInWidget(UIElement *element, Widget *widget) const
    {
        if (element == nullptr || widget == nullptr || widget->Root() == nullptr)
            return false;
        UIElement *node = element;
        while (node != nullptr)
        {
            if (node == widget->Root())
                return true;
            node = node->GetParent();
        }
        return false;
    }

    void UIManager::CleanupWidgetState(Widget *widget)
    {
        if (widget == nullptr)
            return;

        if (_pre_hover_widget == widget)
            _pre_hover_widget = nullptr;
        if (IsElementInWidget(_capture_target, widget))
            _capture_target = nullptr;
        if (IsElementInWidget(_focus_target, widget))
            _focus_target = nullptr;
        if (IsElementInWidget(_hover_target, widget))
            _hover_target = nullptr;
        if (IsElementInWidget(_debug_highlight_target, widget))
            _debug_highlight_target = nullptr;

        for (auto &w: _widgets)
        {
            if (!w)
                continue;
            if (w.get() == widget)
            {
                w->_prev_hover_path.clear();
                w->ResetClickState();
                continue;
            }
            std::erase_if(w->_prev_hover_path, [&](UIElement *element) -> bool
                          { return IsElementInWidget(element, widget); });
            if (IsElementInWidget(w->_last_click_target, widget))
                w->ResetClickState();
        }
    }
}// namespace Ailu::UI
