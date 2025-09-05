#ifndef __COMMON_VIEW__
#define __COMMON_VIEW__
#include "Dock/DockWindow.h"
#include "generated/CommonView.gen.h"
namespace Ailu
{
    namespace UI
    {
        class Image;
        class VerticalBox;
        class ScrollView;
    }// namespace UI
    namespace Editor
    {
        ACLASS()
        class CommonView : public DockWindow
        {
            GENERATED_BODY()
        public:
            CommonView();
            void Update(f32 dt) final;
        private:
            UI::ScrollView *_root = nullptr;
            UI::VerticalBox *_vb = nullptr;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__COMMON_VIEW__
