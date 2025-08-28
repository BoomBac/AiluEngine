//
// Created by 22292 on 2024/10/29.
//

#include "Inc/UI/Widget.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/CommandBuffer.h"
#include "Render/Texture.h"
#include "UI/UIRenderer.h"
#include <Render/TextRenderer.h>

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
            _root->SetDesiredRect(0.0f, 0.0f, _size.x, _size.y);
        }

        void Widget::Update()
        {
            Vector2f loc_mouse_pos = Input::GetMousePos() - _position;
            if (_root)
            {
                const auto& s = _root->_slot;
                _root->SetDesiredRect(s._position.x + _position.x, s._position.y + _position.y,s._size.x,s._size.y);
                _root->Update(0.16f);
            }
            //TextRenderer::DrawText(std::format("MousePos: ({}, {})", loc_mouse_pos.x, loc_mouse_pos.y), Vector2f(0, 0));
            //f32 depth = 0.0f;
            //for (auto child: _children)
            //{
            //    auto &s = _slots[child->ID()]._slot;
            //    {
            //        child->SetDesiredRect(s._position.x, s._position.y, s._size.x, s._size.y);
            //        child->SetDepth(depth);
            //        depth += 0.1f;
            //    }
            //}
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
            _position = position;
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

        void Widget::OnEvent(UIEvent &event)
        {
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
                return;
            }
            UIElement *target = nullptr;
            for (auto ele: *_root)
            {
                target = ele->HitTest(event._mouse_position);
                if (target)
                    break;
            }
            if (target)
                LOG_INFO("target is {}",target->Name());
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
                    event._is_handled = DispatchEvent(e);
                }

                // Enter: 从 i 开始的 currHoverPath
                for (size_t j = i; j < cur_hover_path.size(); j++)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseEnter;
                    e._mouse_position = event._mouse_position;
                    e._target = cur_hover_path[j];
                    event._is_handled = DispatchEvent(e);
                }
            }
            // 4. 生成基本事件（Move / Down / Up / Click / Scroll）
            if (target || _capture_target)
            {
                // 鼠标移动
                if (event._type == UIEvent::EType::kMouseMove)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseMove;
                    e._mouse_position = event._mouse_position;
                    if (_capture_target)
                    {
                        e._target = _capture_target;
                        event._is_handled = DispatchEvent(e);
                    }
                    e._target = target;
                    event._is_handled = DispatchEvent(e);// 从 currTarget 开始冒泡
                }

                // 鼠标按下
                if (event._type == UIEvent::EType::kMouseDown)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseDown;
                    e._mouse_position = event._mouse_position;
                    e._target = target;
                    event._is_handled = DispatchEvent(e);

                    _capture_target = target;// 记录捕获目标
                }
                // 鼠标释放
                if (event._type == UIEvent::EType::kMouseUp)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseUp;
                    e._mouse_position = event._mouse_position;

                    if (_capture_target)
                    {
                        // Up 始终交给 capture_target
                        e._target = _capture_target;
                        event._is_handled = DispatchEvent(e);

                        // 判断 Click（按下和抬起在同一元素上）
                        if (_capture_target == target)
                        {
                            UIEvent click(event);
                            click._type = UIEvent::EType::kMouseClick;
                            click._mouse_position = event._mouse_position;
                            click._target = _capture_target;
                            event._is_handled = DispatchEvent(click);
                        }
                        _capture_target = nullptr;// 清理捕获
                    }
                }
                // 鼠标滚轮
                if (event._type == UIEvent::EType::kMouseScroll)
                {
                    UIEvent e(event);
                    e._type = UIEvent::EType::kMouseScroll;
                    e._mouse_position = event._mouse_position;
                    e._target = target;
                    event._is_handled = DispatchEvent(e);
                }
                _prev_hover_path = std::move(cur_hover_path);
            }
        }// namespace UI
    }// namespace UI
}// namespace Ailu