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
        private:

        };
    }
}// namespace Ailu
#endif