#include "Graph/GraphPreviewWindow.h"
#include "Graph/GraphCanvas.h"
#include "UI/Basic.h"
#include "UI/Container.h"

namespace Ailu
{
    namespace Editor
    {
        GraphPreviewWindow::GraphPreviewWindow() : DockWindow("Graph Canvas Preview", Vector2f(960.0f, 620.0f))
        {
            SetPosition({160.0f, 90.0f});
            _preview_asset = MakeScope<GraphAsset>("GraphCanvasPreview");
            _document = MakeScope<GraphDocument>();
            _document->Open(_preview_asset.get());
            BuildPreviewGraph();

            _content_root->ClearChildren();
            _content_root->CornerRadius(6.0f);
            _canvas = _content_root->AddChild<GraphCanvas>(_document.get());
            _canvas->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _canvas->SetView({170.0f, 130.0f}, 1.0f);
        }

        GraphPreviewWindow::~GraphPreviewWindow() = default;

        void GraphPreviewWindow::BuildPreviewGraph()
        {
            const Guid entry_id = _document->AddNode("Flow.Entry", {0.0f, 10.0f});
            const Guid branch_id = _document->AddNode("Flow.Branch", {250.0f, 0.0f});
            const Guid bool_id = _document->AddNode("Literal.Bool", {250.0f, 190.0f});
            const Guid print_true_id = _document->AddNode("Flow.Print", {540.0f, -20.0f});
            const Guid print_false_id = _document->AddNode("Flow.Print", {540.0f, 150.0f});

            const GraphNodeData *entry = _document->FindNode(entry_id);
            const GraphNodeData *branch = _document->FindNode(branch_id);
            const GraphNodeData *literal_bool = _document->FindNode(bool_id);
            const GraphNodeData *print_true = _document->FindNode(print_true_id);
            const GraphNodeData *print_false = _document->FindNode(print_false_id);
            if (entry == nullptr || branch == nullptr || literal_bool == nullptr || print_true == nullptr || print_false == nullptr)
                return;

            _document->AddLink(entry->_pins[0]._id, branch->_pins[0]._id);
            _document->AddLink(literal_bool->_pins[0]._id, branch->_pins[1]._id);
            _document->AddLink(branch->_pins[2]._id, print_true->_pins[0]._id);
            _document->AddLink(branch->_pins[3]._id, print_false->_pins[0]._id);
        }
    } // namespace Editor
} // namespace Ailu
