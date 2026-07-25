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

namespace Ailu::UI
{
    static UIManager* g_pUIManager = nullptr;
    //----------------------------------------------------------------------------------------UIManager-----------------------------------------------------------------------------
    void UIManager::Init()
    {
        g_pUIManager = new UIManager();
    }
    void UIManager::Shutdown()
    {
        UIRenderer::Shutdown();
        delete g_pUIManager; g_pUIManager = nullptr;
    }
    UIManager *UIManager::Get()
    {
        return g_pUIManager;
    }

    UIManager::UIManager()
    {
        UIRenderer::Init();
        _ui_layer = new UILayer();
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
        LOG_INFO("{}: BringToFront", GetThreadName());
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
    void UIManager::ShowPopupAt(f32 x, f32 y, Ref<UIElement> root, std::function<void()> on_close, Window *win)
    {
        if (!root)
            return;

        Window *target_window = win ? win : &Application::Get().GetWindow();
        auto popup_widget = MakeRef<Widget>();
        popup_widget->Name(std::format("PopupWidget_{}", _popup_stack.size()));
        auto popup_root = MakeRef<Canvas>();
        popup_root->Name(std::format("{}Root", popup_widget->Name()));
        popup_root->AddChild(root);
        popup_widget->AddToWidget(popup_root);
        const Vector2f popup_size = popup_root->MeasureDesiredSize();
        const Vector2f window_size = {(f32) target_window->GetWidth(), (f32) target_window->GetHeight()};
        constexpr f32 kScreenPadding = 4.0f;
        Vector2f popup_pos{x, y};
        if (popup_pos.x + popup_size.x + kScreenPadding > window_size.x)
            popup_pos.x = x - popup_size.x;
        if (popup_pos.y + popup_size.y + kScreenPadding > window_size.y)
            popup_pos.y = y - popup_size.y;
        popup_pos.x = std::clamp(popup_pos.x, kScreenPadding, std::max(kScreenPadding, window_size.x - popup_size.x - kScreenPadding));
        popup_pos.y = std::clamp(popup_pos.y, kScreenPadding, std::max(kScreenPadding, window_size.y - popup_size.y - kScreenPadding));

        popup_widget->SetPosition(popup_pos);
        popup_widget->_visibility = EVisibility::kVisible;
        popup_widget->SetParent(target_window);
        popup_widget->BindOutput(RenderTexture::WindowBackBuffer(target_window));
        popup_widget->SetSize(popup_size);
        RegisterWidget(popup_widget);
        BringToFront(popup_widget.get());
        _popup_stack.push_back({popup_widget, on_close});
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
    Widget *UIManager::GetPopupWidget() const
    {
        if (_popup_stack.empty())
            return nullptr;
        return _popup_stack.back()._widget.get();
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
            if (auto it = std::find_if(w->_prev_hover_path.begin(), w->_prev_hover_path.end(), [&](UIElement *e)
                                       { return e == element; });
                it != w->_prev_hover_path.end())
            {
                w->_prev_hover_path.erase(it, w->_prev_hover_path.end());
                break;
            }
        }
    }
    ZoneHandle UIManager::RegisterInteractionZone(Vector4f rect)
    {
        u32 index;
        if (!_free_indices.empty())
        {
            index = _free_indices.back();
            _free_indices.pop_back();
            _interaction_zones[index]._rect = rect;
        }
        else
        {
            index = (u32)_interaction_zones.size();
            _interaction_zones.push_back({rect, 0});
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
}// namespace Ailu::UI
