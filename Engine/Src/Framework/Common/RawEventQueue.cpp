#include "Framework/Common/RawEventQueue.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Framework/Events/WindowEvent.h"

namespace Ailu
{
    namespace Core
    {
        Event *RawEventQueue::Pop(u8 *event_mem)
        {
            if (auto opt = _events.Pop(); opt.has_value())
            {
                EventData data = opt.value();
                EEventType type = static_cast<EEventType>(data._type);
                memcpy(event_mem, data._payload, MAX_EVENT_SIZE);
                if (type == EEventType::kMouseButtonPressed)
                {
                    return reinterpret_cast<MouseButtonPressedEvent *>(event_mem);
                }
                else if (type == EEventType::kMouseButtonReleased)
                {
                    return reinterpret_cast<MouseButtonReleasedEvent *>(event_mem);
                }
                else if (type == EEventType::kMouseMoved)
                {
                    return reinterpret_cast<MouseMovedEvent *>(event_mem);
                }
                else if (type == EEventType::kMouseScroll)
                {
                    return reinterpret_cast<MouseScrollEvent *>(event_mem);
                }
                else if (type == EEventType::kKeyPressed)
                {
                    return reinterpret_cast<KeyPressedEvent *>(event_mem);
                }
                else if (type == EEventType::kKeyReleased)
                {
                    return reinterpret_cast<KeyReleasedEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowClose)
                {
                    return reinterpret_cast<WindowCloseEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowResize)
                {
                    return reinterpret_cast<WindowResizeEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowFocus)
                {
                    return reinterpret_cast<WindowFocusEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowLostFocus)
                {
                    return reinterpret_cast<WindowLostFocusEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowMoved)
                {
                    return reinterpret_cast<WindowMovedEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowMinimize)
                {
                    return reinterpret_cast<WindowMinimizeEvent *>(event_mem);
                }
                else if (type == EEventType::kWindowMaximize)
                {
                    return reinterpret_cast<WindowMaximizeEvent *>(event_mem);
                }
                else 
                {
                    // Unknown event type
                    return nullptr;
                }
            }
            return nullptr;
        }
    }
}// namespace Ailu


