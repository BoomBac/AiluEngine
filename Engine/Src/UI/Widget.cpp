//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/Widget.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/CommandBuffer.h"
#include "Render/Texture.h"
#include "UI/UIRenderer.h"
#include "UI/UIFramework.h"

namespace Ailu
{
    using namespace Render;
    namespace UI
    {
        Widget::Widget()
        {
            _name = "Widget";
            //_color = RenderTexture::Create(1920, 1080, _name, ERenderTargetFormat::kDefault);
            //_depth = RenderTexture::Create(1920, 1080, _name, ERenderTargetFormat::kDepth);
            _size = Vector2f(1920, 1080);
            _scale = Vector2f(1.0f, 1.0f);
            _position = Vector2f::kZero;
            _color = nullptr;
            _depth = nullptr;
            _root = nullptr;
            BindOutput(RenderTexture::s_backbuffer, nullptr);
        }
        Widget::~Widget()
        {
            if (_root)
                _root.reset();
            _prev_hover_path.clear();
            if (UIManager::Get()->_pre_hover_widget == this)
            {
                UIManager::Get()->_pre_hover_widget = nullptr;
            }
        }
        void Widget::Serialize(FArchive &ar)
        {
            SerializeObject::Serialize(ar);
            if (_root)
            {
                if (auto sar = dynamic_cast<FStructedArchive *>(&ar); sar != nullptr)
                {
                    sar->BeginObject("_root");
                    _root->Serialize(ar);
                    sar->EndObject();
                }
            }
        }
        void Widget::Deserialize(FArchive &ar)
        {
            SerializeObject::Deserialize(ar);
            if (auto sar = dynamic_cast<FStructedArchive *>(&ar); sar != nullptr)
            {
                Type *root_type = nullptr;
                sar->BeginObject("_root");
                {
                    String type_name;
                    sar->BeginObject("_type_name");
                    *sar >> type_name;
                    sar->EndObject();
                    root_type = Type::Find(type_name);
                }
                if (root_type != nullptr)
                    _root.reset(root_type->CreateInstance<UIElement>());
                _root->Deserialize(ar);
                sar->EndObject();
            }
        }
        void Widget::PostDeserialize()
        {
            if (_root)
                _root->PostDeserialize();
        }
        void Widget::BindOutput(RenderTexture *color, RenderTexture *depth)
        {
            _external_color = color;
            _external_depth = depth;
            _is_external_output = true;
        }

        void Widget::AddToWidget(Ref<UIElement> root)
        {
            if (_root)
                LOG_WARNING("Widget::AddToWidget: Root() already exists,replace to ()", _root->Name(),root->Name());
            _root = root;
            _root->Translate(_position);
            _root->Arrange(0.0f, 0.0f, _size.x, _size.y);
        }

        void Widget::PreUpdate(f32 dt)
        {
            if (_root)
                _root->PreUpdate(dt);
        }

        void Widget::Update(f32 dt)
        {
            if (_root)
            {
                //const auto& s = _root->SlotSize();
                //const auto& p = _root->SlotPosition();
                //_root->Arrange(p.x + _position.x, p.y + _position.y,s.x,s.y);
                _root->Update(dt);
            }
        }
        void Widget::PostUpdate(f32 dt)
        {
            if (_root)
                _root->PostUpdate(dt);
        }
        void Ailu::UI::Widget::Render(UIRenderer &r)
        {
            if (!_is_external_output && (_size.x > _color->Width() || _size.y > _color->Height()))
            {
                _color.reset();
                _depth.reset();
                _color = RenderTexture::Create((u16) _size.x, (u16) _size.y, _name, ERenderTargetFormat::kDefault);
                _depth = RenderTexture::Create((u16) _size.x, (u16) _size.y, _name, ERenderTargetFormat::kDepth);
            }
            if (_root)
                _root->Render(r);
        }

        Vector2f Widget::GetSize() const
        {
            return _size;
        }
        void Widget::SetPosition(Vector2f position)
        {
            if (NearbyEqual(position, _position))
                return;
            _position = position;
            if (_root)
                _root->Translate(position);
        }

        bool Widget::DispatchEvent(UIEvent& e)
        {
            UIElement *node = e._target;
            while (node)
            {
                e._current_target = node;
                node->OnEvent(e);
                if (e._is_handled) 
                    break;// 拦截冒泡
                node = node->GetParent();
            }
            return e._is_handled;
        }

        bool Widget::OnEvent(UIEvent &event)
        {
            bool is_event_gen = false;
            if (event._type == UIEvent::EType::kMouseExitWindow)
            {
                for (auto it = _prev_hover_path.rbegin(); it != _prev_hover_path.rend(); ++it)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseExit;
                    e._mouse_position = {-1, -1};// 失效位置
                    e._target = *it;
                    event._is_handled = DispatchEvent(e);
                }
                _prev_hover_path.clear();
                return false;
            }

            if (event._type == UIEvent::EType::kMouseDown && IsHover(event._mouse_position))
            {
                UI::UIManager::Get()->BringToFront(this);
            }
            UIElement *focus = UIManager::Get()->GetFocusedElement();
            if (focus)
            {
                if (event._type == UIEvent::EType::kKeyDown)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kKeyDown;
                    e._target = focus;
                    e._key_code = event._key_code;
                    event._is_handled = DispatchEvent(e);
                }
                else if (event._type == UIEvent::EType::kKeyUp)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kKeyUp;
                    e._target = focus;
                    e._key_code = event._key_code;
                    event._is_handled = DispatchEvent(e);
                }
            }
            UIElement *target = nullptr;
            for (auto& ele: *_root)
            {
                target = ele->HitTest(event._mouse_position);
                if (target)
                    break;
            }
            if (event._type == UIEvent::EType::kMouseDown)
            {
                UIManager::Get()->SetFocus(target);
            }
            //if (target)
            //    LOG_INFO("target is {}",target->Name());
            // 2. 构造 HoverPath
            Vector<UIElement *> cur_hover_path;
            if (target)
            {
                UIElement *node = target;
                while (node)
                {
                    cur_hover_path.push_back(node);
                    node = node->GetParent();
                }
                std::reverse(cur_hover_path.begin(), cur_hover_path.end());// root → child
            }
            // 3. 比较 prevHoverPath 与 currHoverPath，生成 Enter/Exit
            {
                size_t i = 0;
                while (i < _prev_hover_path.size() && i < cur_hover_path.size() && _prev_hover_path[i] == cur_hover_path[i])
                {
                    i++;// 找公共前缀
                }
                // Exit: 从 i 开始的 prevHoverPath
                for (size_t j = _prev_hover_path.size(); j > i; j--)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseExit;
                    e._mouse_position = event._mouse_position;
                    e._target = _prev_hover_path[j - 1];
                    e._current_target = e._target;
                    _prev_hover_path[j - 1]->OnEvent(e);
                    //event._is_handled = DispatchEvent(e);
                    is_event_gen = true;
                    //LOG_INFO("Event stop at widget: {},event: {}", _name,e.ToString());
                }

                // Enter: 从 i 开始的 currHoverPath
                for (size_t j = i; j < cur_hover_path.size(); j++)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseEnter;
                    e._mouse_position = event._mouse_position;
                    e._target = cur_hover_path[j];
                    e._current_target = e._target;
                    cur_hover_path[j]->OnEvent(e);
                    //event._is_handled = DispatchEvent(e);
                    is_event_gen = true;

                    //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                }
                _prev_hover_path = std::move(cur_hover_path);
            }
            // 4. 生成基本事件（Move / Down / Up / Click / Scroll）
            if (target || UIManager::Get()->_capture_target)
            {
                is_event_gen = true;
                // 鼠标移动
                if (event._type == UIEvent::EType::kMouseMove)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseMove;
                    e._mouse_position = event._mouse_position;
                    if (UIManager::Get()->_capture_target)
                    {
                        e._target = UIManager::Get()->_capture_target;
                        event._is_handled = DispatchEvent(e);
                        //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                    }
                    e._target = target;
                    event._is_handled = DispatchEvent(e);// 从 currTarget 开始冒泡
                    //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                }
                // 鼠标按下
                else if (event._type == UIEvent::EType::kMouseDown)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseDown;
                    e._mouse_position = event._mouse_position;
                    e._target = target;
                    event._is_handled = DispatchEvent(e);
                    //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                    UIManager::Get()->_capture_target = target;// 记录捕获目标
                }
                // 鼠标释放
                else if (event._type == UIEvent::EType::kMouseUp)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseUp;
                    e._mouse_position = event._mouse_position;

                    if (UIManager::Get()->_capture_target)
                    {
                        // Up 始终交给 capture_target
                        e._target = UIManager::Get()->_capture_target;
                        event._is_handled = DispatchEvent(e);
                        //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                        // 判断 Click（按下和抬起在同一元素上）
                        if (UIManager::Get()->_capture_target == target)
                        {
                            UIEvent click(event);
                            click._type = UIEvent::EType::kMouseClick;
                            click._mouse_position = event._mouse_position;
                            click._target = UIManager::Get()->_capture_target;
                            click._key_code = event._key_code;
                            event._is_handled = DispatchEvent(click);
                            //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                        }
                        UIManager::Get()->_capture_target = nullptr;// 清理捕获
                    }
                }
                // 鼠标滚轮
                else if (event._type == UIEvent::EType::kMouseScroll)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseScroll;
                    e._mouse_position = event._mouse_position;
                    e._target = target;
                    e._scroll_delta = event._scroll_delta;
                    event._is_handled = DispatchEvent(e);
                    //LOG_INFO("Event stop at widget: {},event: {}", _name, e.ToString());
                }
                else 
                {
                    is_event_gen = false;
                }
            }
            return is_event_gen;
        }
        bool Widget::IsHover(Vector2f pos) const
        {
            return UIElement::IsPointInside(pos,Vector4f{_position.x,_position.y,_size.x,_size.y});
        }
        // namespace UI
    }// namespace UI
}// namespace Ailu