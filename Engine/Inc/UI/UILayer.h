#ifndef __UI_LAYER_H__
#define __UI_LAYER_H__
#include "Framework/Events/Layer.h"
namespace Ailu
{
    namespace UI
    {
        class Widget;
        class AILU_API UILayer : public Layer
        {
        public:
            UILayer();
            UILayer(const String& name);
            ~UILayer();

            void OnAttach() final;
            void OnDetach() final;
            void OnEvent(Ailu::Event& e) final;
            void OnImguiRender() final;

            void RegisterWidget(Widget* w) 
            {
                if (auto it = std::find_if(_widgets.begin(),_widgets.end(),[&](Widget* e)->bool{return e == w;}); it != _widgets.end())
                    return;
                _widgets.push_back(w);
            };
            void UnRegisterWidget(Widget* w)
            {
                _widgets.erase(std::remove_if(_widgets.begin(),_widgets.end(),[&](Widget* e)->bool{return e == w;}),_widgets.end());
            };
        private:
            Vector<Widget*> _widgets;
        };
    }
}// namespace Ailu
#endif