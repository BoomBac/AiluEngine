#pragma once

#include "Graph/GraphAsset.h"
#include "Graph/GraphSchema.h"
#include "Graph/GraphValidation.h"
#include "Framework/Core/Containers/Map.h"
#include <span>

namespace Ailu
{
    struct AILU_API GraphGuidHasher
    {
        size_t operator()(const Guid &guid) const;
    };

    struct AILU_API GraphDocumentSnapshot
    {
        Vector<GraphNodeData> _nodes;
        Vector<GraphLinkData> _links;
        Vector<GraphCommentData> _comments;
    };

    class GraphDocument;

    class AILU_API IGraphCommand
    {
    public:
        virtual ~IGraphCommand() = default;
        virtual bool Execute(GraphDocument &document) = 0;
        virtual void Undo(GraphDocument &document) = 0;
        virtual void Redo(GraphDocument &document) = 0;
        virtual const String &Name() const = 0;
    };

    class AILU_API GraphCommandStack
    {
    public:
        GraphCommandStack() = default;
        explicit GraphCommandStack(GraphDocument *document);
        GraphCommandStack(const GraphCommandStack &) = delete;
        GraphCommandStack &operator=(const GraphCommandStack &) = delete;
        GraphCommandStack(GraphCommandStack &&) noexcept = default;
        GraphCommandStack &operator=(GraphCommandStack &&) noexcept = default;

        bool Execute(Scope<IGraphCommand> command);
        void Undo();
        void Redo();
        void Clear();
        bool CanUndo() const;
        bool CanRedo() const;
        const String &UndoName() const;
        const String &RedoName() const;
        void SetDocument(GraphDocument *document);

    private:
        GraphDocument *_document = nullptr;
        Vector<Scope<IGraphCommand>> _undo_stack;
        Vector<Scope<IGraphCommand>> _redo_stack;
    };

    struct AILU_API GraphNodePosition
    {
        Guid _node_id;
        Vector2f _position = Vector2f::kZero;
    };

    struct AILU_API GraphCommentPosition
    {
        Guid _comment_id;
        Vector2f _position = Vector2f::kZero;
    };

    class AILU_API GraphDocument
    {
    public:
        GraphDocument();
        ~GraphDocument();

        bool Open(GraphAsset *asset);
        void Close();
        bool Apply();
        void Revert();
        bool IsDirty() const { return _is_dirty; }
        GraphAsset *Asset() const { return _asset; }

        const Vector<GraphNodeData> &Nodes() const { return _editing_nodes; }
        const Vector<GraphLinkData> &Links() const { return _editing_links; }
        const Vector<GraphCommentData> &Comments() const { return _editing_comments; }
        const Vector<GraphValidationMessage> &ValidationMessages() const { return _validation_messages; }

        GraphNodeData *FindNode(const Guid &node_id);
        const GraphNodeData *FindNode(const Guid &node_id) const;
        GraphCommentData *FindComment(const Guid &comment_id);
        const GraphCommentData *FindComment(const Guid &comment_id) const;
        GraphPinData *FindPin(const Guid &pin_id);
        const GraphPinData *FindPin(const Guid &pin_id) const;
        GraphLinkData *FindLink(const Guid &link_id);
        const GraphLinkData *FindLink(const Guid &link_id) const;
        GraphNodeData *FindNodeByPin(const Guid &pin_id);
        const GraphNodeData *FindNodeByPin(const Guid &pin_id) const;
        Vector<const GraphLinkData *> FindLinksForPin(const Guid &pin_id) const;
        Vector<const GraphLinkData *> FindLinksForNode(const Guid &node_id) const;

        Guid AddNode(StringView node_type, Vector2f position);
        Guid AddComment(GraphCommentData comment);
        bool RemoveNodes(std::span<const Guid> node_ids);
        bool RemoveComments(std::span<const Guid> comment_ids);
        bool SetNodePositions(std::span<const GraphNodePosition> positions);
        bool SetCommentPositions(std::span<const GraphCommentPosition> positions);
        GraphConnectionResponse CanConnect(const Guid &first_pin, const Guid &second_pin) const;
        Guid AddLink(const Guid &first_pin, const Guid &second_pin);
        bool RemoveLinks(std::span<const Guid> link_ids);
        bool SetNodeProperty(const Guid &node_id, String property_data);
        bool SetCommentTitle(const Guid &comment_id, String title);
        bool SetPinDefaultValue(const Guid &pin_id, String default_value);

        IGraphSchema *Schema() const { return _schema.get(); }
        GraphCommandStack &Commands() { return _command_stack; }
        void RebuildIndices();
        void Validate();

    private:
        friend class GraphSnapshotCommand;
        friend class AddGraphNodeCommand;
        friend class AddGraphNodeWithConnectionCommand;
        friend class AddGraphCommentCommand;
        friend class RemoveGraphNodesCommand;
        friend class RemoveGraphCommentsCommand;
        friend class MoveGraphNodesCommand;
        friend class MoveGraphSelectionCommand;
        friend class AddGraphRerouteNodeCommand;
        friend class AddGraphLinkCommand;
        friend class RemoveGraphLinksCommand;
        friend class SetGraphNodePropertyCommand;
        friend class SetGraphCommentTitleCommand;
        friend class SetGraphPinDefaultValueCommand;
        friend class PasteGraphElementsCommand;

        bool WouldCreateCycle(const Guid &output_pin, const Guid &input_pin) const;
        GraphDocumentSnapshot CaptureSnapshot() const;
        void RestoreSnapshot(const GraphDocumentSnapshot &snapshot);
        bool PasteElements(std::span<const GraphNodeData> nodes, std::span<const GraphLinkData> links,
                           std::span<const GraphCommentData> comments = {});
        void RepairLoadedNodePins();
        void CanonicalizeLoadedLinks();
        void MarkDirty();

    private:
        GraphAsset *_asset = nullptr;
        Vector<GraphNodeData> _original_nodes;
        Vector<GraphLinkData> _original_links;
        Vector<GraphCommentData> _original_comments;
        Vector<GraphNodeData> _editing_nodes;
        Vector<GraphLinkData> _editing_links;
        Vector<GraphCommentData> _editing_comments;
        HashMap<Guid, u32, GraphGuidHasher> _node_index;
        HashMap<Guid, std::pair<u32, u32>, GraphGuidHasher> _pin_index;
        HashMap<Guid, u32, GraphGuidHasher> _link_index;
        Scope<IGraphSchema> _schema;
        GraphCommandStack _command_stack;
        Vector<GraphValidationMessage> _validation_messages;
        bool _is_dirty = false;
    };

    class AILU_API GraphSnapshotCommand : public IGraphCommand
    {
    public:
        explicit GraphSnapshotCommand(String name);
        void Undo(GraphDocument &document) override;
        void Redo(GraphDocument &document) override;
        const String &Name() const override { return _name; }

    protected:
        virtual bool Apply(GraphDocument &document) = 0;
        bool ExecuteWithSnapshots(GraphDocument &document);

    protected:
        String _name;
        GraphDocumentSnapshot _before;
        GraphDocumentSnapshot _after;
        bool _has_snapshot = false;
    };

    class AILU_API AddGraphNodeCommand final : public GraphSnapshotCommand
    {
    public:
        AddGraphNodeCommand(StringView node_type, Vector2f position);
        bool Execute(GraphDocument &document) override;
        Guid NodeId() const { return _node_id; }

    private:
        bool Apply(GraphDocument &document) override;

    private:
        String _node_type;
        Vector2f _position = Vector2f::kZero;
        Guid _node_id = Guid::EmptyGuid();
    };

    class AILU_API AddGraphNodeWithConnectionCommand final : public GraphSnapshotCommand
    {
    public:
        AddGraphNodeWithConnectionCommand(StringView node_type, Vector2f position, Guid source_pin = Guid::EmptyGuid());
        bool Execute(GraphDocument &document) override;
        Guid NodeId() const { return _node_id; }
        Guid LinkId() const { return _link_id; }

    private:
        bool Apply(GraphDocument &document) override;

    private:
        String _node_type;
        Vector2f _position = Vector2f::kZero;
        Guid _source_pin = Guid::EmptyGuid();
        Guid _node_id = Guid::EmptyGuid();
        Guid _link_id = Guid::EmptyGuid();
    };

    class AILU_API RemoveGraphNodesCommand final : public GraphSnapshotCommand
    {
    public:
        explicit RemoveGraphNodesCommand(Vector<Guid> node_ids);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Vector<Guid> _node_ids;
    };

    class AILU_API AddGraphCommentCommand final : public GraphSnapshotCommand
    {
    public:
        explicit AddGraphCommentCommand(GraphCommentData comment);
        bool Execute(GraphDocument &document) override;
        Guid CommentId() const { return _comment_id; }

    private:
        bool Apply(GraphDocument &document) override;

    private:
        GraphCommentData _comment;
        Guid _comment_id = Guid::EmptyGuid();
    };

    class AILU_API RemoveGraphCommentsCommand final : public GraphSnapshotCommand
    {
    public:
        explicit RemoveGraphCommentsCommand(Vector<Guid> comment_ids);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Vector<Guid> _comment_ids;
    };

    class AILU_API MoveGraphNodesCommand final : public GraphSnapshotCommand
    {
    public:
        MoveGraphNodesCommand(Vector<GraphNodePosition> start_positions, Vector<GraphNodePosition> target_positions);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Vector<GraphNodePosition> _start_positions;
        Vector<GraphNodePosition> _target_positions;
    };

    class AILU_API MoveGraphSelectionCommand final : public GraphSnapshotCommand
    {
    public:
        MoveGraphSelectionCommand(Vector<GraphNodePosition> start_node_positions,
                                  Vector<GraphNodePosition> target_node_positions,
                                  Vector<GraphCommentPosition> start_comment_positions,
                                  Vector<GraphCommentPosition> target_comment_positions);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Vector<GraphNodePosition> _start_node_positions;
        Vector<GraphNodePosition> _target_node_positions;
        Vector<GraphCommentPosition> _start_comment_positions;
        Vector<GraphCommentPosition> _target_comment_positions;
    };

    class AILU_API AddGraphLinkCommand final : public GraphSnapshotCommand
    {
    public:
        AddGraphLinkCommand(Guid first_pin, Guid second_pin);
        bool Execute(GraphDocument &document) override;
        Guid LinkId() const { return _link_id; }

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Guid _first_pin = Guid::EmptyGuid();
        Guid _second_pin = Guid::EmptyGuid();
        Guid _link_id = Guid::EmptyGuid();
    };

    class AILU_API AddGraphRerouteNodeCommand final : public GraphSnapshotCommand
    {
    public:
        AddGraphRerouteNodeCommand(Guid link_id, Vector2f position);
        bool Execute(GraphDocument &document) override;
        Guid NodeId() const { return _node_id; }

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Guid _link_id = Guid::EmptyGuid();
        Guid _node_id = Guid::EmptyGuid();
        Vector2f _position = Vector2f::kZero;
    };

    class AILU_API RemoveGraphLinksCommand final : public GraphSnapshotCommand
    {
    public:
        explicit RemoveGraphLinksCommand(Vector<Guid> link_ids);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Vector<Guid> _link_ids;
    };

    class AILU_API SetGraphNodePropertyCommand final : public GraphSnapshotCommand
    {
    public:
        SetGraphNodePropertyCommand(Guid node_id, String property_data);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Guid _node_id = Guid::EmptyGuid();
        String _property_data;
    };

    class AILU_API SetGraphCommentTitleCommand final : public GraphSnapshotCommand
    {
    public:
        SetGraphCommentTitleCommand(Guid comment_id, String title);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Guid _comment_id = Guid::EmptyGuid();
        String _title;
    };

    class AILU_API SetGraphPinDefaultValueCommand final : public GraphSnapshotCommand
    {
    public:
        SetGraphPinDefaultValueCommand(Guid pin_id, String default_value);
        bool Execute(GraphDocument &document) override;

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Guid _pin_id = Guid::EmptyGuid();
        String _default_value;
    };

    class AILU_API PasteGraphElementsCommand final : public GraphSnapshotCommand
    {
    public:
        PasteGraphElementsCommand(Vector<GraphNodeData> nodes, Vector<GraphLinkData> links,
                                  Vector<GraphCommentData> comments = {});
        bool Execute(GraphDocument &document) override;
        const Vector<Guid> &PastedNodeIds() const { return _pasted_node_ids; }
        const Vector<Guid> &PastedLinkIds() const { return _pasted_link_ids; }

    private:
        bool Apply(GraphDocument &document) override;

    private:
        Vector<GraphNodeData> _nodes;
        Vector<GraphLinkData> _links;
        Vector<GraphCommentData> _comments;
        Vector<Guid> _pasted_node_ids;
        Vector<Guid> _pasted_link_ids;
    };
} // namespace Ailu
