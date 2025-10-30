#include "Widgets/InputLayer.h"
#include "Ext/imgui/imgui.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/KeyEvent.h"
#include "Framework/Events/MouseEvent.h"
#include "Render/Camera.h"

namespace Ailu
{
    namespace Editor
    {
        InputLayer::InputLayer() : Layer("InputLayer")
        {
        }
        InputLayer::InputLayer(const String &name) : InputLayer()
        {
            _debug_name = name;
        }
        InputLayer::~InputLayer()
        {
        }
        void InputLayer::OnAttach()
        {

        }
        void InputLayer::OnDetach()
        {

        }
        void InputLayer::OnEvent(Event &e)
        {
        }

        void InputLayer::OnImguiRender()
        {

        }
        void InputLayer::OnUpdate(float delta_time)
        {
        }

    }// namespace Editor
}// namespace Ailu
