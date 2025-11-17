#ifndef __ASSETBROWSER_H__
#define __ASSETBROWSER_H__
#include "Dock/DockWindow.h"
#include "generated/AssetBrowser.gen.h"
namespace Ailu
{
    class Asset;
    namespace Render
    {
        class Mesh;
        class RenderTexture;
    }
    namespace UI
    {
        class Image;
        class SplitView;
        class Canvas;
        class VerticalBox;
        class Slider;
        class Text;
        class ScrollView;
    }// namespace UI
    namespace Editor
    {
        ACLASS()
        class AssetBrowser : public DockWindow
        {
            GENERATED_BODY()
        public:
            AssetBrowser();
            void Update(f32 dt) final;

        private:
            inline static const f32 kDragThreshold = 5.0f;
            UI::SplitView *_sv = nullptr;
            UI::VerticalBox *_right = nullptr;
            UI::ScrollView *_icon_area = nullptr;
            UI::Canvas * _icon_content = nullptr;
            UI::Text *_path_title;

            bool _is_dirty = true;
            f32 _icon_size = 64.0f;
            UI::UIElement *_hover_item = nullptr;
            fs::path _current_path;
            Vector<Asset *> _cur_dir_assets;
            HashMap<Render::Mesh *, Ref<Render::RenderTexture>> _mesh_preview_icons;
            bool _is_dragging = false;
            Vector2f _drag_start_pos;
        };
    }// namespace Editor
}// namespace Ailu
#endif// !__ASSETBROWSER_H__
