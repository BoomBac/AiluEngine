#include "Dock/DockWindow.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "Render/GraphicsContext.h"
#include "Common/EditorStyle.h"
#include "Framework/Common/Input.h"


namespace Ailu
{
    namespace Editor
    {
        static void UpdateResizeMouseCursor(u32 resize_dir)
        {
            UI::ECursorType cursor_type = UI::ECursorType::kArrow;

            // 左右
            if (resize_dir == 1 || resize_dir == 4)
                cursor_type = UI::ECursorType::kSizeEW;
            // 上下
            else if (resize_dir == 2 || resize_dir == 8)
                cursor_type = UI::ECursorType::kSizeNS;
            // 对角 ↘↖
            else if ((resize_dir & 1 && resize_dir & 2) || (resize_dir & 4 && resize_dir & 8))
                cursor_type = UI::ECursorType::kSizeNWSE;
            // 对角 ↗↙
            else if ((resize_dir & 4 && resize_dir & 2) || (resize_dir & 1 && resize_dir & 8))
                cursor_type = UI::ECursorType::kSizeNESW;

            UI::UIManager::Get()->SetCursor(cursor_type);
        }

        DockWindow::DockWindow(const String &title, Vector2f size) : _is_focused(false), _is_dragging(false)
        {
            _drag_start_mouse_pos = {-1.0f, 0.0f};
            _title_bar = MakeRef<UI::Widget>();
            _title_bar->Name(std::format("{}_title", title));
            auto c = MakeRef<UI::Canvas>();
            c->_slot._size = Vector2f(size.x, kTitleBarHeight);
            _title_bar_root = c->AddChild<UI::Border>();
            _title_bar_root->_thickness = 0.0f;
            auto hb = _title_bar_root->AddChild<UI::HorizontalBox>();
            _title_bar_root->OnMouseDown() += [&](UI::UIEvent &e)
            {
                _is_dragging = true;
                _drag_start_mouse_pos = e._mouse_position;
                _drag_start_offset = _rect.xy - _drag_start_mouse_pos;
                e._is_handled = true;
                _is_focused = true;
            };
            _title_bar_root->OnMouseUp() += [&](UI::UIEvent &e)
            {
                _is_dragging = false;
                _drag_start_mouse_pos = {-1.0f, 0.0f};
                _is_focused = false;
            };
            _title_bar_root->OnMouseMove() += [&](UI::UIEvent& e) {
                if (_is_dragging)
                {
                    _rect.xy = e._mouse_position + _drag_start_offset;
                }
            };
            _title = hb->AddChild<UI::Text>();
            _title->_text = title;
            _btn_close = hb->AddChild<UI::Button>();
            _btn_close->OnMouseClick() += [this](UI::UIEvent& e)
            { 
                LOG_INFO("DockWindow({}) close...", _title->_text);
            };
            _title_bar->AddToWidget(c);

            _content = MakeRef<UI::Widget>();
            _content->SetPosition(_position + Vector2f(0.0f, kTitleBarHeight));
            c = MakeRef<UI::Canvas>();
            c->_slot._size = Vector2f(size.x, size.y - kTitleBarHeight);
            _content_root = c->AddChild<UI::Border>();
            _content_root-> _thickness = kBorderThickness;
            _content_root->_border_color = g_editor_style._window_border_color;
            _content_root->_bg_color = g_editor_style._window_bg_color;
            _content->AddToWidget(c);
            _content->Name(std::format("{}_content", title));
            _rect = Vector4f(_position.x, _position.y, size.x, size.y);
            _content_root->OnMouseMove() += [&](UI::UIEvent &e)
            {

            };
        }
        DockWindow::~DockWindow()
        {
        }
        void DockWindow::SetRect(Vector4f rect)
        {
            _rect = rect;
        }
        void DockWindow::Update(f32 dt)
        {
            _title_bar->SetPosition(_rect.xy);
            _title_bar->SetSize({_rect.z,kTitleBarHeight});
            _title_bar->Root()->_slot._size = Vector2f(_rect.z, kTitleBarHeight);
            _title_bar_root->_slot._size = Vector2f(_rect.z, kTitleBarHeight);
            _title_bar_root->_bg_color = _is_focused ? Colors::kBlue : g_editor_style._window_title_bar_color;
            _title->_slot._size.y = kTitleBarHeight;
            _btn_close->_slot._size = Vector2f(kTitleBarHeight, kTitleBarHeight);

            _content->SetPosition(_rect.xy + Vector2f(0.0f, kTitleBarHeight));
            _content->SetSize({_rect.z, _rect.w - kTitleBarHeight});
            _content->Root()->_slot._size = Vector2f(_rect.z, _rect.w - kTitleBarHeight);
            _content_root->_slot._size = Vector2f(_rect.z, _rect.w - kTitleBarHeight);
            _content_root->_border_color = g_editor_style._window_border_color;
            _content_root->_bg_color = g_editor_style._window_bg_color;
            const bool is_left_mouse_down = Input::IsMouseButtonPressed(1u);
            if (s_cur_resizing_window == nullptr)
            {
                UpdateResizeState(Input::GetMousePos(), is_left_mouse_down);
            }
            else
            {
                if (_is_resizing)
                {
                    if (is_left_mouse_down && _resize_dir & 4)
                    {
                        _rect.z += Input::GetMousePos().x - _pre_mouse_pos.x;
                    }
                    if (is_left_mouse_down && _resize_dir & 8)
                    {
                        _rect.w += Input::GetMousePos().y - _pre_mouse_pos.y;
                    }
                    _is_resizing = is_left_mouse_down;
                    if (!_is_resizing)
                    {
                        s_cur_resizing_window = nullptr;
                        _resize_dir = 0u;
                    }
                }
            }
            if (_resize_dir)
                UpdateResizeMouseCursor(_resize_dir);
            _pre_mouse_pos = Input::GetMousePos();
        }
        void DockWindow::UpdateResizeState(Vector2f mouse_pos, bool is_mouse_down)
        {
            _resize_dir = 0;

            // 判断边缘
            if (mouse_pos.x >= _rect.x && mouse_pos.x <= _rect.x + kBorderThickness)
                _resize_dir |= 1;// 左
            if (mouse_pos.x >= _rect.x + _rect.z - kBorderThickness && mouse_pos.x <= _rect.x + _rect.z)
                _resize_dir |= 4;// 右
            if (mouse_pos.y >= _rect.y && mouse_pos.y <= _rect.y + kBorderThickness)
                _resize_dir |= 2;// 上
            if (mouse_pos.y >= _rect.y + _rect.w - kBorderThickness && mouse_pos.y <= _rect.y + _rect.w)
                _resize_dir |= 8;// 下

            _is_resizing = is_mouse_down && _resize_dir != 0;
            if (_is_resizing)
            {
                s_cur_resizing_window = this;
                _pre_mouse_pos = mouse_pos;
            }
        }

    }// namespace Editor
}
