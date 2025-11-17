#ifndef __COMMON_VIEW__
#define __COMMON_VIEW__
#include "Dock/DockWindow.h"
#include "generated/ObjectDetail.gen.h"
namespace Ailu
{
    namespace UI
    {
        class Image;
        class VerticalBox;
        class ScrollView;
        class InputBlock;
        class CollapsibleView;
    }// namespace UI
    namespace Editor
    {
        ACLASS()
        class ObjectDetail : public DockWindow
        {
            GENERATED_BODY()
        public:
            ObjectDetail();
            ~ObjectDetail() override;
            void Update(f32 dt) final;

        private:
            UI::ScrollView *_root = nullptr;
            UI::VerticalBox *_vb = nullptr;
            Array<UI::InputBlock*,3> _pos_block;
            Array<UI::InputBlock*,3> _rot_block;
            Array<UI::InputBlock*,3> _scale_block;
            UI::CollapsibleView *_light_block = nullptr;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__COMMON_VIEW__
