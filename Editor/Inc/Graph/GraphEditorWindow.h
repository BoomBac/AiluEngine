#pragma once

#include "Editors/AssetEditor.h"
#include "Graph/GraphAsset.h"
#include "Graph/GraphDocument.h"
#include "UI/Basic.h"
#include "UI/Container.h"

namespace Ailu
{
    namespace Editor
    {
        class GraphCanvas;

        class GraphEditorWindow final : public AssetEditor
        {
        public:
            GraphEditorWindow();
            ~GraphEditorWindow() override;

            using AssetEditor::Open;
            void Update(f32 dt) override;
            bool Open(GraphAsset *asset);
            void SaveDockLayoutState(JsonArchive &ar) override;
            void LoadDockLayoutState(JsonArchive &ar) override;
            void OnDockLayoutLoaded() override;

        protected:
            void OnClose() override;
            void OnBeforeSave() override;
            void OnAssetSaved() override;
            void OnAssetReloaded() override;

        private:
            void BuildContent();
            void BuildToolbar(UI::HorizontalBox *toolbar);
            void BuildPalette(UI::VerticalBox *palette);
            void BuildCenterPanel(UI::Border *center);
            void BuildDetails(UI::VerticalBox *details);
            void BuildStatusBar(UI::HorizontalBox *status_bar);
            void BuildPreviewGraph();
            void RefreshDetails();
            void AddValidationPanel();
            void RefreshStatusBar();
            void RefreshWindowTitle();
            void AddNodeFromPalette(const String &node_type);
            void FocusValidationMessage(const GraphValidationMessage &message);
            String MakeSelectionSignature() const;
            static String PinDirectionText(EGraphPinDirection direction);

        private:
            GraphAsset *_asset = nullptr;
            Scope<GraphAsset> _preview_asset;
            Scope<GraphDocument> _document;
            GraphCanvas *_canvas = nullptr;
            UI::SplitView *_main_split = nullptr;
            UI::SplitView *_right_split = nullptr;
            UI::VerticalBox *_palette_root = nullptr;
            UI::VerticalBox *_details_root = nullptr;
            UI::Text *_status_text = nullptr;
            UI::Button *_save_button = nullptr;
            f32 _left_panel_ratio = 0.18f;
            f32 _right_panel_ratio = 0.74f;
            Vector2f _saved_view_offset = {170.0f, 130.0f};
            f32 _saved_zoom = 1.0f;
            String _details_selection_signature;
            bool _last_known_dirty = false;
        };
    } // namespace Editor
} // namespace Ailu
