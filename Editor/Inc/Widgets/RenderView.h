#ifndef __RENDER_VIEW__
#define __RENDER_VIEW__
#include "Dock/DockWindow.h"
#include "generated/RenderView.gen.h"
namespace Ailu
{
    namespace UI
    {
        class Image;
        class VerticalBox;
    }
    namespace Editor
    {
        ACLASS()
        class RenderView : public DockWindow
        {
            GENERATED_BODY()
        public:
            RenderView();
            void Update(f32 dt) final;
            void SetSource(Render::Texture *tex);
        private:
            UI::VerticalBox *_vb = nullptr;
            UI::Image *_source = nullptr;
        };
    }
}
#endif// !__RENDER_VIEW__
