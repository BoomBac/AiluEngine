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
        DESTORY_PTR(g_pUIManager);
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
        auto popup_widget = MakeRef<Widget>();
        popup_widget->Name("PopupWidget");
        popup_widget->AddToWidget(MakeRef<Canvas>());
        popup_widget->_visibility = EVisibility::kHide;
        RegisterWidget(popup_widget);
        _popup_widget = popup_widget.get();
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
        if (it == _widgets.end()-1)
            return;
        Ref<Widget> current = *it;
        if (_widgets.size() > 1)
            _widgets.back()->_on_lost_focus_delegate.Invoke();
        if (it == _widgets.end() - 1)
            return;//already in front
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
        _popup_widget->SetPosition({x, y});
        _popup_widget->_visibility = EVisibility::kVisible;
        _popup_widget->BindOutput(RenderTexture::WindowBackBuffer(win? win : &Application::Get().GetWindow()));
        _popup_widget->Root()->AddChild(root);
        BringToFront(_popup_widget);
        _popup_widget->SetSize(_popup_widget->Root()->MeasureDesiredSize());
        if (on_close)
            _on_popup_close = on_close;
    }
    void UIManager::HidePopup()
    {
        if (_popup_widget)
        {
            _popup_widget->_visibility = EVisibility::kHide;
            if (!_popup_widget->Root()->GetChildren().empty())
                LOG_INFO("UIManager::HidePopup: destory element {}", _popup_widget->Root()->ChildAt(0u)->Name())
            _popup_widget->Root()->ClearChildren();
            if (_on_popup_close)
            {
                _on_popup_close();
                _on_popup_close = nullptr;
            }
        }
    }
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
            index = _interaction_zones.size();
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
