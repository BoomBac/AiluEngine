#pragma once

#include "Dock/DockWindow.h"
#include "Graph/GraphAsset.h"
#include "Graph/GraphDocument.h"

namespace Ailu
{
    namespace Editor
    {
        class GraphCanvas;

        class GraphPreviewWindow final : public DockWindow
        {
        public:
            GraphPreviewWindow();
            ~GraphPreviewWindow() override;

        private:
            void BuildPreviewGraph();

        private:
            Scope<GraphAsset> _preview_asset;
            Scope<GraphDocument> _document;
            GraphCanvas *_canvas = nullptr;
        };
    } // namespace Editor
} // namespace Ailu
