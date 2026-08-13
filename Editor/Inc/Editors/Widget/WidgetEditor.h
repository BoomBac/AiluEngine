#pragma once
#ifndef __WIDGET_EDITOR_H__
#define __WIDGET_EDITOR_H__

#include "Dock/DockWindow.h"
#include "Framework/Core/SmartPtr.h"
#include "Framework/Core/String.h"
#include "UI/TreeView.h"

namespace Ailu
{
    class WidgetAsset;

    namespace UI
    {
        class Button;
        class CheckBox;
        class HorizontalBox;
        class Text;
        class VerticalBox;
        class Widget;
    }

    namespace Render
    {
        class RenderTexture;
    }

    namespace Editor
    {
        class ReflectedPropertyPanel;
        class WidgetDesignerPreview;

        class WidgetEditor : public DockWindow
        {
            friend class WidgetPropertyEditCommand;
            friend class WidgetCanvasEditCommand;
            friend class WidgetDesignerPreview;
        public:
            WidgetEditor();
            ~WidgetEditor() override;

            void Update(f32 dt) override;
            void Open(WidgetAsset *asset);
            void Close();
            void Save();
            void MarkDirty();
            void RequestClose() override;
            void SetSelectedElement(UI::UIElement *element, bool sync_hierarchy = true);
            WidgetAsset *GetAsset() const { return _asset; }
            bool IsDirty() const { return _is_asset_dirty; }

        private:
            void BuildUi();
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildPalette(UI::VerticalBox *parent, const String &filter = {});
            void RefreshPanels();
            void RefreshHierarchy();
            void RefreshDetails();
            void RefreshDesigner();
            void RefreshDirtyState();
            void RebuildPreview();
            void DestroyPreview();
            void FitDesigner();
            void SetPreviewMode(bool is_preview);
            void RefreshModeState();
            void ForwardPreviewEvent(const UI::UIEvent &event);
            void ClosePreviewPopup();
            bool IsPreviewElement(UI::UIElement *element) const;
            UI::UIElement *ResolveAssetElement(UI::UIElement *preview_element) const;
            UI::UIElement *ResolvePreviewElement(UI::UIElement *asset_element) const;
            Vector2f ScreenToDesign(Vector2f screen_pos) const;
            Vector2f DesignToScreen(Vector2f design_pos) const;
            void OnPropertyChanging(const PropertyInfo &property, void *instance, UI::UIElement *owner);
            void OnPropertyChanged(const PropertyInfo &property, void *instance, UI::UIElement *owner);
            void ApplyPropertySnapshot(const PropertyInfo *property, void *instance, UI::UIElement *owner, const String &snapshot);
            void ApplyCanvasEdit(UI::UIElement *element, Vector2f position, Vector2f size, bool size_to_content);
            void AddPaletteElement(const Type *element_type, UI::UIElement *parent, Vector2f design_position);
            void DeleteSelectedElement();
            void RenameSelectedElement(Vector2f popup_pos);
            void ReorderSelectedElement(i32 direction);
            void ReparentElement(UI::UIElement *source, UI::UIElement *new_parent);
            void ShowHierarchyContextMenu(UI::TreeItemId item, Vector2f popup_pos);
            void OnHierarchyDrop(UI::TreeItemId source_item, UI::TreeItemId target_item);
            bool CanReparent(UI::UIElement *source, UI::UIElement *new_parent) const;

        private:
            WidgetAsset *_asset = nullptr;
            UI::Text *_asset_name = nullptr;
            UI::Text *_design_size = nullptr;
            UI::Text *_preview_size = nullptr;
            UI::Text *_designer_label = nullptr;
            UI::Text *_designer_zoom_label = nullptr;
            UI::Text *_status_text = nullptr;
            UI::VerticalBox *_hierarchy_content = nullptr;
            UI::VerticalBox *_details_content = nullptr;
            UI::ScrollView *_details_scroll_view = nullptr;
            UI::TreeView *_hierarchy_tree = nullptr;
            UI::Button *_save_button = nullptr;
            UI::Button *_undo_button = nullptr;
            UI::Button *_redo_button = nullptr;
            UI::Button *_design_mode_button = nullptr;
            UI::Button *_preview_mode_button = nullptr;
            UI::Button *_fit_button = nullptr;
            UI::CheckBox *_grid_checkbox = nullptr;
            WidgetDesignerPreview *_designer_preview = nullptr;
            UI::UIElement *_selected_element = nullptr;
            UI::UIElement *_hovered_element = nullptr;
            bool _is_asset_dirty = false;
            bool _is_preview_mode = false;
            bool _preview_rebuild_pending = false;
            bool _show_designer_grid = true;
            bool _designer_needs_fit = true;
            f32 _designer_zoom = 1.0f;
            Vector2f _designer_pan = Vector2f::kZero;
            Ref<UI::Widget> _preview_widget;
            Ref<Render::RenderTexture> _preview_render_texture;
            UI::UIElement *_preview_capture_element = nullptr;
            UI::Widget *_preview_popup_widget = nullptr;

            struct PropertySnapshot
            {
                const PropertyInfo *_property = nullptr;
                void *_instance = nullptr;
                UI::UIElement *_owner = nullptr;
                String _before;
            };
            PropertySnapshot _pending_property_snapshot;

            class WidgetTreeDataSource;
            Scope<WidgetTreeDataSource> _tree_data_source;
            Scope<ReflectedPropertyPanel> _element_property_panel;
            Scope<ReflectedPropertyPanel> _slot_property_panel;
        };
    }// namespace Editor
}// namespace Ailu

#endif
