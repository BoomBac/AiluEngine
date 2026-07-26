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
            UI::CollapsibleView *_transform_block = nullptr;
            UI::CollapsibleView *_light_block = nullptr;
            UI::CollapsibleView *_static_mesh_block = nullptr;
            UI::CollapsibleView *_light_probe_block = nullptr;
            UI::CollapsibleView *_cam_block = nullptr;
            UI::CollapsibleView *_sprite_block = nullptr;
            UI::CollapsibleView *_script_block = nullptr;
            UI::InputBlock *_script_path_block = nullptr;
            UI::UIElement *_prev_comp_block = nullptr;
            bool _needs_rebuild = true;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__COMMON_VIEW__
