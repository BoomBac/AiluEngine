#pragma once
#ifndef __WIDGET_EDITOR_H__
#define __WIDGET_EDITOR_H__

#include "Editors/AssetEditor.h"
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
        class InputBlock;
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
        struct WidgetEditSession;

        class WidgetEditor : public AssetEditor
        {
            friend class WidgetDesignerPreview;
        public:
            WidgetEditor();
            ~WidgetEditor() override;

            void Update(f32 dt) override;
            using AssetEditor::Open;
            void Open(WidgetAsset *asset);
            void MarkDirty();
            void SetSelectedElement(UI::UIElement *element, bool sync_hierarchy = true);
            WidgetAsset *GetWidgetAsset() const { return _asset; }

        protected:
            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;
            void OnClose() override;

        private:
            void BuildUi();
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildPalette(UI::VerticalBox *parent, const String &filter = {});
            void RefreshPanels();
            void RefreshHierarchy();
            void RefreshDetails();
            void RefreshDesigner();
            void RefreshDirtyState();
            void RefreshWorkingState();
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
            void FlushPendingPropertyEdit();
            void ApplyCanvasEdit(UI::UIElement *element, Vector2f position, Vector2f size, bool size_to_content, Vector2f anchor);
            void CommitCanvasEdit(UI::UIElement *element, Vector2f before_position, Vector2f before_size,
                                  bool before_size_to_content, Vector2f before_anchor);
            void RefreshPendingCanvasEdit();
            void CommitPropertyEdit(UI::UIElement *element, const String &property_name, bool is_slot, String before,
                                    String after);
            void QueuePropertyEdit(UI::UIElement *element, const String &property_name, bool is_slot, String before,
                                   String after);
            void FlushDeferredPropertyEdit();
            bool IsDetailValueDragActive() const;
            void RefreshPendingPropertyEdit();
            void SyncPreviewProperty(UI::UIElement *element, const String &property_name, bool is_slot,
                                     const String &snapshot);
            void AddPaletteElement(const Type *element_type, UI::UIElement *parent, Vector2f design_position);
            void DuplicateSelectedElement();
            void DeleteSelectedElement();
            void RenameSelectedElement(Vector2f popup_pos);
            void ReorderSelectedElement(i32 direction);
            void ReparentElement(UI::UIElement *source, UI::UIElement *new_parent);
            void InsertElement(UI::UIElement *source, UI::UIElement *target, bool insert_before);
            void ShowHierarchyContextMenu(UI::TreeItemId item, Vector2f popup_pos);
            void OnHierarchyDrop(UI::TreeItemId source_item, UI::TreeItemId target_item);
            bool CanReparent(UI::UIElement *source, UI::UIElement *new_parent) const;
            bool CanInsertElement(UI::UIElement *source, UI::UIElement *target, bool insert_before) const;
            bool CanAcceptChild(const UI::UIElement *parent, const UI::UIElement *child) const;
            WidgetAsset *WorkingAsset() const { return _working_asset.get(); }
            UI::UIElement *WorkingRoot() const;
            Ref<UI::UIElement> CloneWorkingRoot() const;
            void CommitTreeEdit(Ref<UI::UIElement> before_root, const Guid &before_selection);
            void ApplyWorkingSnapshot(const Ref<UI::UIElement> &root, const Guid &selected_guid);
            bool IsWorkingTreeSaved() const;
            void SyncRuntimeFromWorking();

        private:
            WidgetAsset *_asset = nullptr;
            Ref<WidgetAsset> _working_asset;
            Ref<UI::UIElement> _saved_root;
            Vector2f _saved_design_size = {1920.0f, 1080.0f};
            Ref<WidgetEditSession> _edit_session;
            UI::Text *_asset_name = nullptr;
            UI::Text *_design_size = nullptr;
            UI::Text *_preview_size = nullptr;
            UI::Text *_designer_label = nullptr;
            UI::Text *_designer_zoom_label = nullptr;
            UI::InputBlock *_design_width_input = nullptr;
            UI::InputBlock *_design_height_input = nullptr;
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
            Guid _selected_guid = Guid::EmptyGuid();
            UI::UIElement *_hovered_element = nullptr;
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
                bool _is_slot = false;
            };
            PropertySnapshot _pending_property_snapshot;

            struct DeferredPropertyEdit
            {
                Guid _element_guid = Guid::EmptyGuid();
                String _property_name;
                String _before;
                String _after;
                bool _is_slot = false;
            };
            DeferredPropertyEdit _deferred_property_edit;

            class WidgetTreeDataSource;
            Scope<WidgetTreeDataSource> _tree_data_source;
            Scope<ReflectedPropertyPanel> _element_property_panel;
            Scope<ReflectedPropertyPanel> _slot_property_panel;
        };
    }// namespace Editor
}// namespace Ailu

#endif
