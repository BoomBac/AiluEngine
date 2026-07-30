#include "Graph/GraphDocument.h"
#include "Graph/Flow/FlowGraphNodes.h"
#include "Graph/GraphNodeRegistry.h"
#include <algorithm>
#include <set>

namespace Ailu
{
    namespace
    {
        const String kEmptyCommandName;
    }

    size_t GraphGuidHasher::operator()(const Guid &guid) const
    {
        return std::hash<String>{}(guid.ToString());
    }

    GraphCommandStack::GraphCommandStack(GraphDocument *document) : _document(document)
    {
    }

    bool GraphCommandStack::Execute(Scope<IGraphCommand> command)
    {
        if (_document == nullptr || command == nullptr || !command->Execute(*_document))
            return false;
        _undo_stack.emplace_back(std::move(command));
        _redo_stack.clear();
        return true;
    }

    void GraphCommandStack::Undo()
    {
        if (_document == nullptr || _undo_stack.empty())
            return;
        Scope<IGraphCommand> command = std::move(_undo_stack.back());
        _undo_stack.pop_back();
        command->Undo(*_document);
        _redo_stack.emplace_back(std::move(command));
    }

    void GraphCommandStack::Redo()
    {
        if (_document == nullptr || _redo_stack.empty())
            return;
        Scope<IGraphCommand> command = std::move(_redo_stack.back());
        _redo_stack.pop_back();
        command->Redo(*_document);
        _undo_stack.emplace_back(std::move(command));
    }

    void GraphCommandStack::Clear()
    {
        _undo_stack.clear();
        _redo_stack.clear();
    }

    bool GraphCommandStack::CanUndo() const
    {
        return !_undo_stack.empty();
    }

    bool GraphCommandStack::CanRedo() const
    {
        return !_redo_stack.empty();
    }

    const String &GraphCommandStack::UndoName() const
    {
        return !_undo_stack.empty() ? _undo_stack.back()->Name() : kEmptyCommandName;
    }

    const String &GraphCommandStack::RedoName() const
    {
        return !_redo_stack.empty() ? _redo_stack.back()->Name() : kEmptyCommandName;
    }

    void GraphCommandStack::SetDocument(GraphDocument *document)
    {
        _document = document;
        Clear();
    }

    GraphSnapshotCommand::GraphSnapshotCommand(String name) : _name(std::move(name))
    {
    }

    bool GraphSnapshotCommand::ExecuteWithSnapshots(GraphDocument &document)
    {
        _before = document.CaptureSnapshot();
        if (!Apply(document))
        {
            document.RestoreSnapshot(_before);
            return false;
        }
        _after = document.CaptureSnapshot();
        _has_snapshot = true;
        return true;
    }

    void GraphSnapshotCommand::Undo(GraphDocument &document)
    {
        if (_has_snapshot)
            document.RestoreSnapshot(_before);
    }

    void GraphSnapshotCommand::Redo(GraphDocument &document)
    {
        if (_has_snapshot)
            document.RestoreSnapshot(_after);
    }

    AddGraphNodeCommand::AddGraphNodeCommand(StringView node_type, Vector2f position) :
        GraphSnapshotCommand("Add Node"), _node_type(node_type), _position(position)
    {
    }

    bool AddGraphNodeCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool AddGraphNodeCommand::Apply(GraphDocument &document)
    {
        _node_id = document.AddNode(_node_type, _position);
        return _node_id != Guid::EmptyGuid();
    }

    AddGraphNodeWithConnectionCommand::AddGraphNodeWithConnectionCommand(StringView node_type, Vector2f position,
                                                                         Guid source_pin) :
        GraphSnapshotCommand(source_pin == Guid::EmptyGuid() ? "Add Node" : "Add Node And Link"),
        _node_type(node_type), _position(position), _source_pin(source_pin)
    {
    }

    bool AddGraphNodeWithConnectionCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool AddGraphNodeWithConnectionCommand::Apply(GraphDocument &document)
    {
        _node_id = document.AddNode(_node_type, _position);
        _link_id = Guid::EmptyGuid();
        if (_node_id == Guid::EmptyGuid())
            return false;
        if (_source_pin == Guid::EmptyGuid())
            return true;

        GraphNodeData *node = document.FindNode(_node_id);
        if (node == nullptr)
            return false;

        for (const GraphPinData &pin : node->_pins)
        {
            const GraphConnectionResponse response = document.CanConnect(_source_pin, pin._id);
            if (response._action == EGraphConnectionAction::kDisallow)
                continue;

            _link_id = document.AddLink(_source_pin, pin._id);
            return _link_id != Guid::EmptyGuid();
        }
        return false;
    }

    RemoveGraphNodesCommand::RemoveGraphNodesCommand(Vector<Guid> node_ids) :
        GraphSnapshotCommand("Remove Nodes"), _node_ids(std::move(node_ids))
    {
    }

    bool RemoveGraphNodesCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool RemoveGraphNodesCommand::Apply(GraphDocument &document)
    {
        return document.RemoveNodes(std::span<const Guid>(_node_ids.data(), _node_ids.size()));
    }

    AddGraphCommentCommand::AddGraphCommentCommand(GraphCommentData comment) :
        GraphSnapshotCommand("Add Comment"), _comment(std::move(comment))
    {
    }

    bool AddGraphCommentCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool AddGraphCommentCommand::Apply(GraphDocument &document)
    {
        _comment_id = document.AddComment(std::move(_comment));
        return _comment_id != Guid::EmptyGuid();
    }

    RemoveGraphCommentsCommand::RemoveGraphCommentsCommand(Vector<Guid> comment_ids) :
        GraphSnapshotCommand("Remove Comments"), _comment_ids(std::move(comment_ids))
    {
    }

    bool RemoveGraphCommentsCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool RemoveGraphCommentsCommand::Apply(GraphDocument &document)
    {
        return document.RemoveComments(std::span<const Guid>(_comment_ids.data(), _comment_ids.size()));
    }

    MoveGraphNodesCommand::MoveGraphNodesCommand(Vector<GraphNodePosition> start_positions,
                                                 Vector<GraphNodePosition> target_positions) :
        GraphSnapshotCommand("Move Nodes"), _start_positions(std::move(start_positions)),
        _target_positions(std::move(target_positions))
    {
    }

    bool MoveGraphNodesCommand::Execute(GraphDocument &document)
    {
        if (_start_positions.size() != _target_positions.size())
            return false;
        bool changed = false;
        for (u32 index = 0u; index < _start_positions.size(); ++index)
        {
            if (!(_start_positions[index]._node_id == _target_positions[index]._node_id) ||
                !NearbyEqual(_start_positions[index]._position, _target_positions[index]._position))
            {
                changed = true;
                break;
            }
        }
        return changed && ExecuteWithSnapshots(document);
    }

    bool MoveGraphNodesCommand::Apply(GraphDocument &document)
    {
        return document.SetNodePositions(std::span<const GraphNodePosition>(_target_positions.data(),
                                                                            _target_positions.size()));
    }

    MoveGraphSelectionCommand::MoveGraphSelectionCommand(Vector<GraphNodePosition> start_node_positions,
                                                         Vector<GraphNodePosition> target_node_positions,
                                                         Vector<GraphCommentPosition> start_comment_positions,
                                                         Vector<GraphCommentPosition> target_comment_positions) :
        GraphSnapshotCommand("Move Graph Selection"), _start_node_positions(std::move(start_node_positions)),
        _target_node_positions(std::move(target_node_positions)),
        _start_comment_positions(std::move(start_comment_positions)),
        _target_comment_positions(std::move(target_comment_positions))
    {
    }

    bool MoveGraphSelectionCommand::Execute(GraphDocument &document)
    {
        if (_start_node_positions.size() != _target_node_positions.size() ||
            _start_comment_positions.size() != _target_comment_positions.size())
            return false;

        bool changed = false;
        for (u32 index = 0u; index < _start_node_positions.size(); ++index)
        {
            if (!(_start_node_positions[index]._node_id == _target_node_positions[index]._node_id) ||
                !NearbyEqual(_start_node_positions[index]._position, _target_node_positions[index]._position))
            {
                changed = true;
                break;
            }
        }
        for (u32 index = 0u; !changed && index < _start_comment_positions.size(); ++index)
        {
            if (!(_start_comment_positions[index]._comment_id == _target_comment_positions[index]._comment_id) ||
                !NearbyEqual(_start_comment_positions[index]._position, _target_comment_positions[index]._position))
            {
                changed = true;
                break;
            }
        }
        return changed && ExecuteWithSnapshots(document);
    }

    bool MoveGraphSelectionCommand::Apply(GraphDocument &document)
    {
        bool changed = false;
        changed |= document.SetNodePositions(std::span<const GraphNodePosition>(_target_node_positions.data(),
                                                                                _target_node_positions.size()));
        changed |= document.SetCommentPositions(std::span<const GraphCommentPosition>(_target_comment_positions.data(),
                                                                                      _target_comment_positions.size()));
        return changed;
    }

    AddGraphLinkCommand::AddGraphLinkCommand(Guid first_pin, Guid second_pin) :
        GraphSnapshotCommand("Add Link"), _first_pin(first_pin), _second_pin(second_pin)
    {
    }

    bool AddGraphLinkCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool AddGraphLinkCommand::Apply(GraphDocument &document)
    {
        _link_id = document.AddLink(_first_pin, _second_pin);
        return _link_id != Guid::EmptyGuid();
    }

    AddGraphRerouteNodeCommand::AddGraphRerouteNodeCommand(Guid link_id, Vector2f position) :
        GraphSnapshotCommand("Add Reroute Node"), _link_id(link_id), _position(position)
    {
    }

    bool AddGraphRerouteNodeCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool AddGraphRerouteNodeCommand::Apply(GraphDocument &document)
    {
        const GraphLinkData *link = document.FindLink(_link_id);
        if (link == nullptr)
            return false;
        const Guid output_pin = link->_output_pin;
        const Guid input_pin = link->_input_pin;
        const GraphPinData *source_pin = document.FindPin(output_pin);
        const GraphPinData *target_pin = document.FindPin(input_pin);
        if (source_pin == nullptr || target_pin == nullptr || source_pin->_kind != target_pin->_kind)
            return false;
        const EGraphPinKind reroute_pin_kind = source_pin->_kind;
        const String reroute_value_type = source_pin->_value_type;
        const String reroute_default_value = source_pin->_default_value;

        Vector<Guid> remove_links = {_link_id};
        if (!document.RemoveLinks(std::span<const Guid>(remove_links.data(), remove_links.size())))
            return false;

        _node_id = document.AddNode("Flow.Reroute", _position);
        GraphNodeData *reroute = document.FindNode(_node_id);
        if (reroute == nullptr || reroute->_pins.size() < 2u)
            return false;
        reroute->_pins[0]._kind = reroute_pin_kind;
        reroute->_pins[0]._value_type = reroute_value_type;
        reroute->_pins[0]._default_value = reroute_default_value;
        reroute->_pins[1]._kind = reroute_pin_kind;
        reroute->_pins[1]._value_type = reroute_value_type;
        reroute->_pins[1]._default_value = reroute_default_value;
        const Guid reroute_input = reroute->_pins[0]._id;
        const Guid reroute_output = reroute->_pins[1]._id;
        return document.AddLink(output_pin, reroute_input) != Guid::EmptyGuid() &&
               document.AddLink(reroute_output, input_pin) != Guid::EmptyGuid();
    }

    RemoveGraphLinksCommand::RemoveGraphLinksCommand(Vector<Guid> link_ids) :
        GraphSnapshotCommand("Remove Links"), _link_ids(std::move(link_ids))
    {
    }

    bool RemoveGraphLinksCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool RemoveGraphLinksCommand::Apply(GraphDocument &document)
    {
        return document.RemoveLinks(std::span<const Guid>(_link_ids.data(), _link_ids.size()));
    }

    SetGraphNodePropertyCommand::SetGraphNodePropertyCommand(Guid node_id, String property_data) :
        GraphSnapshotCommand("Set Node Property"), _node_id(node_id), _property_data(std::move(property_data))
    {
    }

    bool SetGraphNodePropertyCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool SetGraphNodePropertyCommand::Apply(GraphDocument &document)
    {
        return document.SetNodeProperty(_node_id, _property_data);
    }

    SetGraphCommentTitleCommand::SetGraphCommentTitleCommand(Guid comment_id, String title) :
        GraphSnapshotCommand("Set Comment Title"), _comment_id(comment_id), _title(std::move(title))
    {
    }

    bool SetGraphCommentTitleCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool SetGraphCommentTitleCommand::Apply(GraphDocument &document)
    {
        return document.SetCommentTitle(_comment_id, _title);
    }

    SetGraphPinDefaultValueCommand::SetGraphPinDefaultValueCommand(Guid pin_id, String default_value) :
        GraphSnapshotCommand("Set Pin Default Value"), _pin_id(pin_id), _default_value(std::move(default_value))
    {
    }

    bool SetGraphPinDefaultValueCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool SetGraphPinDefaultValueCommand::Apply(GraphDocument &document)
    {
        return document.SetPinDefaultValue(_pin_id, _default_value);
    }

    PasteGraphElementsCommand::PasteGraphElementsCommand(Vector<GraphNodeData> nodes, Vector<GraphLinkData> links,
                                                         Vector<GraphCommentData> comments) :
        GraphSnapshotCommand("Paste Graph Elements"), _nodes(std::move(nodes)), _links(std::move(links)),
        _comments(std::move(comments))
    {
    }

    bool PasteGraphElementsCommand::Execute(GraphDocument &document)
    {
        return ExecuteWithSnapshots(document);
    }

    bool PasteGraphElementsCommand::Apply(GraphDocument &document)
    {
        if (!document.PasteElements(std::span<const GraphNodeData>(_nodes.data(), _nodes.size()),
                                    std::span<const GraphLinkData>(_links.data(), _links.size()),
                                    std::span<const GraphCommentData>(_comments.data(), _comments.size())))
        {
            return false;
        }
        _pasted_node_ids.clear();
        _pasted_link_ids.clear();
        for (const GraphNodeData &node : _nodes)
            _pasted_node_ids.emplace_back(node._id);
        for (const GraphLinkData &link : _links)
            _pasted_link_ids.emplace_back(link._id);
        return true;
    }

    GraphDocument::GraphDocument()
    {
        RegisterFlowGraphNodes();
        _command_stack.SetDocument(this);
    }

    GraphDocument::~GraphDocument()
    {
        Close();
    }

    bool GraphDocument::Open(GraphAsset *asset)
    {
        Close();
        if (asset == nullptr)
            return false;

        _asset = asset;
        _schema = CreateGraphSchema(asset->SchemaType().empty() ? "FlowGraphSchema" : asset->SchemaType());
        _original_nodes = asset->Nodes();
        _original_links = asset->Links();
        _original_comments = asset->Comments();
        _editing_nodes = _original_nodes;
        _editing_links = _original_links;
        _editing_comments = _original_comments;
        RepairLoadedNodePins();
        _is_dirty = false;
        RebuildIndices();
        CanonicalizeLoadedLinks();
        RebuildIndices();
        _original_nodes = _editing_nodes;
        _original_links = _editing_links;
        _original_comments = _editing_comments;
        Validate();
        return true;
    }

    void GraphDocument::Close()
    {
        _asset = nullptr;
        _original_nodes.clear();
        _original_links.clear();
        _original_comments.clear();
        _editing_nodes.clear();
        _editing_links.clear();
        _editing_comments.clear();
        _node_index.clear();
        _pin_index.clear();
        _link_index.clear();
        _schema.reset();
        _command_stack.SetDocument(this);
        _validation_messages.clear();
        _is_dirty = false;
    }

    bool GraphDocument::Apply()
    {
        if (_asset == nullptr)
            return false;

        _asset->MutableNodes() = _editing_nodes;
        _asset->MutableLinks() = _editing_links;
        _asset->MutableComments() = _editing_comments;
        _original_nodes = _editing_nodes;
        _original_links = _editing_links;
        _original_comments = _editing_comments;
        _is_dirty = false;
        _command_stack.Clear();
        Validate();
        return true;
    }

    void GraphDocument::Revert()
    {
        _editing_nodes = _original_nodes;
        _editing_links = _original_links;
        _editing_comments = _original_comments;
        _is_dirty = false;
        _command_stack.Clear();
        RebuildIndices();
        Validate();
    }

    GraphNodeData *GraphDocument::FindNode(const Guid &node_id)
    {
        const auto it = _node_index.find(node_id);
        return it != _node_index.end() ? &_editing_nodes[it->second] : nullptr;
    }

    const GraphNodeData *GraphDocument::FindNode(const Guid &node_id) const
    {
        const auto it = _node_index.find(node_id);
        return it != _node_index.end() ? &_editing_nodes[it->second] : nullptr;
    }

    GraphCommentData *GraphDocument::FindComment(const Guid &comment_id)
    {
        const auto it = std::find_if(_editing_comments.begin(), _editing_comments.end(), [&](const GraphCommentData &comment)
        {
            return comment._id == comment_id;
        });
        return it != _editing_comments.end() ? &(*it) : nullptr;
    }

    const GraphCommentData *GraphDocument::FindComment(const Guid &comment_id) const
    {
        const auto it = std::find_if(_editing_comments.begin(), _editing_comments.end(), [&](const GraphCommentData &comment)
        {
            return comment._id == comment_id;
        });
        return it != _editing_comments.end() ? &(*it) : nullptr;
    }

    GraphPinData *GraphDocument::FindPin(const Guid &pin_id)
    {
        const auto it = _pin_index.find(pin_id);
        return it != _pin_index.end() ? &_editing_nodes[it->second.first]._pins[it->second.second] : nullptr;
    }

    const GraphPinData *GraphDocument::FindPin(const Guid &pin_id) const
    {
        const auto it = _pin_index.find(pin_id);
        return it != _pin_index.end() ? &_editing_nodes[it->second.first]._pins[it->second.second] : nullptr;
    }

    GraphLinkData *GraphDocument::FindLink(const Guid &link_id)
    {
        const auto it = _link_index.find(link_id);
        return it != _link_index.end() ? &_editing_links[it->second] : nullptr;
    }

    const GraphLinkData *GraphDocument::FindLink(const Guid &link_id) const
    {
        const auto it = _link_index.find(link_id);
        return it != _link_index.end() ? &_editing_links[it->second] : nullptr;
    }

    GraphNodeData *GraphDocument::FindNodeByPin(const Guid &pin_id)
    {
        const auto it = _pin_index.find(pin_id);
        return it != _pin_index.end() ? &_editing_nodes[it->second.first] : nullptr;
    }

    const GraphNodeData *GraphDocument::FindNodeByPin(const Guid &pin_id) const
    {
        const auto it = _pin_index.find(pin_id);
        return it != _pin_index.end() ? &_editing_nodes[it->second.first] : nullptr;
    }

    Vector<const GraphLinkData *> GraphDocument::FindLinksForPin(const Guid &pin_id) const
    {
        Vector<const GraphLinkData *> links;
        for (const GraphLinkData &link : _editing_links)
        {
            if (link._output_pin == pin_id || link._input_pin == pin_id)
                links.emplace_back(&link);
        }
        return links;
    }

    Vector<const GraphLinkData *> GraphDocument::FindLinksForNode(const Guid &node_id) const
    {
        Vector<const GraphLinkData *> links;
        const GraphNodeData *node = FindNode(node_id);
        if (node == nullptr)
            return links;

        for (const GraphPinData &pin : node->_pins)
        {
            Vector<const GraphLinkData *> pin_links = FindLinksForPin(pin._id);
            links.insert(links.end(), pin_links.begin(), pin_links.end());
        }
        std::sort(links.begin(), links.end());
        links.erase(std::unique(links.begin(), links.end()), links.end());
        return links;
    }

    Guid GraphDocument::AddNode(StringView node_type, Vector2f position)
    {
        if (_schema == nullptr || !_schema->CanCreateNode(*this, node_type))
            return Guid::EmptyGuid();

        GraphNodeData node;
        if (!GraphNodeRegistry::Get().InitializeNode(node_type, node))
            return Guid::EmptyGuid();

        node._position = position;
        const Guid node_id = node._id;
        _editing_nodes.emplace_back(std::move(node));
        RebuildIndices();
        MarkDirty();
        Validate();
        return node_id;
    }

    Guid GraphDocument::AddComment(GraphCommentData comment)
    {
        if (comment._id == Guid::EmptyGuid())
            comment._id = Guid::Generate();
        if (comment._size.x <= 1.0f || comment._size.y <= 1.0f)
            comment._size = {400.0f, 240.0f};
        const Guid comment_id = comment._id;
        _editing_comments.emplace_back(std::move(comment));
        MarkDirty();
        Validate();
        return comment_id;
    }

    bool GraphDocument::RemoveNodes(std::span<const Guid> node_ids)
    {
        if (node_ids.empty())
            return false;

        std::set<String> remove_node_ids;
        std::set<String> remove_pin_ids;
        for (const Guid &node_id : node_ids)
        {
            const GraphNodeData *node = FindNode(node_id);
            if (node == nullptr || (_schema != nullptr && !_schema->CanDeleteNode(*this, *node)))
                continue;
            remove_node_ids.emplace(node_id.ToString());
            for (const GraphPinData &pin : node->_pins)
                remove_pin_ids.emplace(pin._id.ToString());
        }

        if (remove_node_ids.empty())
            return false;

        Vector<std::pair<Guid, Guid>> restored_links;
        for (const Guid &node_id : node_ids)
        {
            const GraphNodeData *node = FindNode(node_id);
            if (node == nullptr || node->_node_type != "Flow.Reroute" || node->_pins.size() < 2u)
                continue;

            const GraphPinData &input_pin = node->_pins[0];
            const GraphPinData &output_pin = node->_pins[1];
            const Vector<const GraphLinkData *> input_links = FindLinksForPin(input_pin._id);
            const Vector<const GraphLinkData *> output_links = FindLinksForPin(output_pin._id);
            const GraphLinkData *incoming_link = nullptr;
            const GraphLinkData *outgoing_link = nullptr;
            for (const GraphLinkData *link : input_links)
            {
                if (link->_input_pin == input_pin._id)
                    incoming_link = link;
            }
            for (const GraphLinkData *link : output_links)
            {
                if (link->_output_pin == output_pin._id)
                    outgoing_link = link;
            }
            if (incoming_link == nullptr || outgoing_link == nullptr)
                continue;
            const GraphNodeData *source_node = FindNodeByPin(incoming_link->_output_pin);
            const GraphNodeData *target_node = FindNodeByPin(outgoing_link->_input_pin);
            if (source_node == nullptr || target_node == nullptr ||
                remove_node_ids.contains(source_node->_id.ToString()) ||
                remove_node_ids.contains(target_node->_id.ToString()))
                continue;
            restored_links.push_back({incoming_link->_output_pin, outgoing_link->_input_pin});
        }

        _editing_links.erase(std::remove_if(_editing_links.begin(), _editing_links.end(), [&](const GraphLinkData &link)
        {
            return remove_pin_ids.contains(link._output_pin.ToString()) ||
                   remove_pin_ids.contains(link._input_pin.ToString());
        }), _editing_links.end());
        _editing_nodes.erase(std::remove_if(_editing_nodes.begin(), _editing_nodes.end(), [&](const GraphNodeData &node)
        {
            return remove_node_ids.contains(node._id.ToString());
        }), _editing_nodes.end());

        RebuildIndices();
        for (const auto &[output_pin, input_pin] : restored_links)
            AddLink(output_pin, input_pin);
        MarkDirty();
        Validate();
        return true;
    }

    bool GraphDocument::RemoveComments(std::span<const Guid> comment_ids)
    {
        if (comment_ids.empty())
            return false;

        std::set<String> remove_comment_ids;
        for (const Guid &comment_id : comment_ids)
        {
            if (FindComment(comment_id) != nullptr)
                remove_comment_ids.emplace(comment_id.ToString());
        }
        if (remove_comment_ids.empty())
            return false;

        const size_t old_size = _editing_comments.size();
        _editing_comments.erase(std::remove_if(_editing_comments.begin(), _editing_comments.end(),
                                               [&](const GraphCommentData &comment)
                                               { return remove_comment_ids.contains(comment._id.ToString()); }),
                                _editing_comments.end());
        if (_editing_comments.size() == old_size)
            return false;

        MarkDirty();
        Validate();
        return true;
    }

    bool GraphDocument::SetNodePositions(std::span<const GraphNodePosition> positions)
    {
        if (positions.empty())
            return false;

        bool changed = false;
        for (const GraphNodePosition &position : positions)
        {
            GraphNodeData *node = FindNode(position._node_id);
            if (node == nullptr || NearbyEqual(node->_position, position._position))
                continue;
            node->_position = position._position;
            changed = true;
        }

        if (!changed)
            return false;

        MarkDirty();
        return true;
    }

    bool GraphDocument::SetCommentPositions(std::span<const GraphCommentPosition> positions)
    {
        if (positions.empty())
            return false;

        bool changed = false;
        for (const GraphCommentPosition &position : positions)
        {
            GraphCommentData *comment = FindComment(position._comment_id);
            if (comment == nullptr || NearbyEqual(comment->_position, position._position))
                continue;
            comment->_position = position._position;
            changed = true;
        }

        if (!changed)
            return false;

        MarkDirty();
        return true;
    }

    GraphConnectionResponse GraphDocument::CanConnect(const Guid &first_pin, const Guid &second_pin) const
    {
        if (_schema == nullptr)
            return GraphConnectionResponse::Disallow("Graph schema is missing.");

        const GraphPinData *first = FindPin(first_pin);
        const GraphPinData *second = FindPin(second_pin);
        if (first == nullptr || second == nullptr)
            return GraphConnectionResponse::Disallow("Pin does not exist.");

        GraphConnectionResponse response = _schema->CanConnect(*this, *first, *second);
        if (response._action == EGraphConnectionAction::kDisallow || _schema->AllowsCycles())
            return response;

        const Guid output_pin = first->_direction == EGraphPinDirection::kOutput ? first->_id : second->_id;
        const Guid input_pin = first->_direction == EGraphPinDirection::kInput ? first->_id : second->_id;
        if (WouldCreateCycle(output_pin, input_pin))
            return GraphConnectionResponse::Disallow("Connection would create a cycle.");
        return response;
    }

    Guid GraphDocument::AddLink(const Guid &first_pin, const Guid &second_pin)
    {
        const GraphConnectionResponse response = CanConnect(first_pin, second_pin);
        if (response._action == EGraphConnectionAction::kDisallow)
            return Guid::EmptyGuid();

        const GraphPinData *first = FindPin(first_pin);
        const GraphPinData *second = FindPin(second_pin);
        if (first == nullptr || second == nullptr)
            return Guid::EmptyGuid();

        const Guid output_pin = first->_direction == EGraphPinDirection::kOutput ? first->_id : second->_id;
        const Guid input_pin = first->_direction == EGraphPinDirection::kInput ? first->_id : second->_id;
        if (response._action == EGraphConnectionAction::kReplaceInput)
        {
            _editing_links.erase(std::remove_if(_editing_links.begin(), _editing_links.end(),
                [&](const GraphLinkData &link) { return link._input_pin == input_pin; }), _editing_links.end());
        }

        GraphLinkData link;
        link._id = Guid::Generate();
        link._output_pin = output_pin;
        link._input_pin = input_pin;
        const Guid link_id = link._id;
        _editing_links.emplace_back(std::move(link));
        RebuildIndices();
        MarkDirty();
        Validate();
        return link_id;
    }

    bool GraphDocument::RemoveLinks(std::span<const Guid> link_ids)
    {
        if (link_ids.empty())
            return false;

        std::set<String> remove_link_ids;
        for (const Guid &link_id : link_ids)
            remove_link_ids.emplace(link_id.ToString());

        const size_t old_size = _editing_links.size();
        _editing_links.erase(std::remove_if(_editing_links.begin(), _editing_links.end(), [&](const GraphLinkData &link)
        {
            return remove_link_ids.contains(link._id.ToString());
        }), _editing_links.end());
        if (_editing_links.size() == old_size)
            return false;

        RebuildIndices();
        MarkDirty();
        Validate();
        return true;
    }

    bool GraphDocument::SetNodeProperty(const Guid &node_id, String property_data)
    {
        GraphNodeData *node = FindNode(node_id);
        if (node == nullptr || node->_property_data == property_data)
            return false;
        node->_property_data = std::move(property_data);
        MarkDirty();
        Validate();
        return true;
    }

    bool GraphDocument::SetCommentTitle(const Guid &comment_id, String title)
    {
        GraphCommentData *comment = FindComment(comment_id);
        if (comment == nullptr || comment->_title == title)
            return false;
        comment->_title = std::move(title);
        MarkDirty();
        Validate();
        return true;
    }

    bool GraphDocument::SetPinDefaultValue(const Guid &pin_id, String default_value)
    {
        GraphPinData *pin = FindPin(pin_id);
        if (pin == nullptr || pin->_default_value == default_value)
            return false;
        pin->_default_value = std::move(default_value);
        MarkDirty();
        Validate();
        return true;
    }

    void GraphDocument::RebuildIndices()
    {
        _node_index.clear();
        _pin_index.clear();
        _link_index.clear();

        for (u32 node_index = 0u; node_index < _editing_nodes.size(); ++node_index)
        {
            _node_index[_editing_nodes[node_index]._id] = node_index;
            for (u32 pin_index = 0u; pin_index < _editing_nodes[node_index]._pins.size(); ++pin_index)
                _pin_index[_editing_nodes[node_index]._pins[pin_index]._id] = {node_index, pin_index};
        }
        for (u32 link_index = 0u; link_index < _editing_links.size(); ++link_index)
            _link_index[_editing_links[link_index]._id] = link_index;
    }

    void GraphDocument::Validate()
    {
        _validation_messages.clear();
        GraphValidation::Validate(*this, _validation_messages);
    }

    void GraphDocument::RepairLoadedNodePins()
    {
        const GraphNodeRegistry &registry = GraphNodeRegistry::Get();
        for (GraphNodeData &node : _editing_nodes)
        {
            const GraphNodeDesc *desc = registry.FindNode(node._node_type);
            if (desc == nullptr)
                continue;

            node._display_name = desc->_display_name;
            if (node._size.x <= 0.0f || node._size.y <= 0.0f)
                node._size = desc->_default_size;
            node._flags = desc->_flags;

            Vector<GraphPinData> repaired_pins;
            repaired_pins.reserve(desc->_pins.size() + node._pins.size());
            Vector<bool> used_pins(node._pins.size(), false);

            for (u32 desc_pin_index = 0u; desc_pin_index < desc->_pins.size(); ++desc_pin_index)
            {
                const GraphPinDesc &pin_desc = desc->_pins[desc_pin_index];
                GraphPinData *loaded_pin = nullptr;
                u32 loaded_pin_index = 0u;
                for (u32 pin_index = 0u; pin_index < node._pins.size(); ++pin_index)
                {
                    if (!used_pins[pin_index] && node._pins[pin_index]._name == pin_desc._name)
                    {
                        loaded_pin = &node._pins[pin_index];
                        loaded_pin_index = pin_index;
                        break;
                    }
                }

                if (loaded_pin == nullptr && desc_pin_index < node._pins.size() && !used_pins[desc_pin_index])
                {
                    loaded_pin = &node._pins[desc_pin_index];
                    loaded_pin_index = desc_pin_index;
                }

                GraphPinData pin;
                if (loaded_pin != nullptr)
                {
                    pin = *loaded_pin;
                    used_pins[loaded_pin_index] = true;
                }
                else
                {
                    pin._id = Guid::Generate();
                }

                pin._name = pin_desc._name;
                pin._value_type = pin_desc._value_type;
                pin._direction = pin_desc._direction;
                pin._kind = pin_desc._kind;
                if (pin._default_value.empty())
                    pin._default_value = pin_desc._default_value;
                repaired_pins.emplace_back(std::move(pin));
            }

            for (u32 pin_index = 0u; pin_index < node._pins.size(); ++pin_index)
            {
                if (!used_pins[pin_index] && node._pins[pin_index]._is_dynamic)
                    repaired_pins.emplace_back(std::move(node._pins[pin_index]));
            }
            node._pins = std::move(repaired_pins);
        }
    }

    void GraphDocument::CanonicalizeLoadedLinks()
    {
        for (GraphLinkData &link : _editing_links)
        {
            const GraphPinData *output_pin = FindPin(link._output_pin);
            const GraphPinData *input_pin = FindPin(link._input_pin);
            if (output_pin == nullptr || input_pin == nullptr)
                continue;
            if (output_pin->_direction == EGraphPinDirection::kInput &&
                input_pin->_direction == EGraphPinDirection::kOutput)
            {
                std::swap(link._output_pin, link._input_pin);
            }
        }
    }

    GraphDocumentSnapshot GraphDocument::CaptureSnapshot() const
    {
        return {_editing_nodes, _editing_links, _editing_comments};
    }

    void GraphDocument::RestoreSnapshot(const GraphDocumentSnapshot &snapshot)
    {
        _editing_nodes = snapshot._nodes;
        _editing_links = snapshot._links;
        _editing_comments = snapshot._comments;
        RebuildIndices();
        MarkDirty();
        Validate();
    }

    bool GraphDocument::PasteElements(std::span<const GraphNodeData> nodes, std::span<const GraphLinkData> links,
                                      std::span<const GraphCommentData> comments)
    {
        if (nodes.empty() && links.empty() && comments.empty())
            return false;

        std::set<String> pasted_node_ids;
        std::set<String> pasted_pin_ids;
        for (const GraphNodeData &node : nodes)
        {
            if (node._id == Guid::EmptyGuid() || FindNode(node._id) != nullptr ||
                !pasted_node_ids.emplace(node._id.ToString()).second)
            {
                return false;
            }
            for (const GraphPinData &pin : node._pins)
            {
                if (pin._id == Guid::EmptyGuid() || FindPin(pin._id) != nullptr ||
                    !pasted_pin_ids.emplace(pin._id.ToString()).second)
                {
                    return false;
                }
            }
        }

        std::set<String> pasted_link_ids;
        for (const GraphLinkData &link : links)
        {
            if (link._id == Guid::EmptyGuid() || FindLink(link._id) != nullptr ||
                !pasted_link_ids.emplace(link._id.ToString()).second ||
                !pasted_pin_ids.contains(link._output_pin.ToString()) ||
                !pasted_pin_ids.contains(link._input_pin.ToString()))
            {
                return false;
            }
        }

        _editing_nodes.insert(_editing_nodes.end(), nodes.begin(), nodes.end());
        _editing_links.insert(_editing_links.end(), links.begin(), links.end());
        _editing_comments.insert(_editing_comments.end(), comments.begin(), comments.end());
        RebuildIndices();
        MarkDirty();
        Validate();
        return true;
    }

    bool GraphDocument::WouldCreateCycle(const Guid &output_pin, const Guid &input_pin) const
    {
        const GraphNodeData *source_node = FindNodeByPin(output_pin);
        const GraphNodeData *target_node = FindNodeByPin(input_pin);
        if (source_node == nullptr || target_node == nullptr || source_node->_id == target_node->_id)
            return true;

        Vector<Guid> stack = {target_node->_id};
        std::set<String> visited;
        while (!stack.empty())
        {
            const Guid node_id = stack.back();
            stack.pop_back();
            if (node_id == source_node->_id)
                return true;
            if (!visited.emplace(node_id.ToString()).second)
                continue;

            const GraphNodeData *node = FindNode(node_id);
            if (node == nullptr)
                continue;
            for (const GraphPinData &pin : node->_pins)
            {
                if (pin._direction != EGraphPinDirection::kOutput)
                    continue;
                for (const GraphLinkData *link : FindLinksForPin(pin._id))
                {
                    const GraphNodeData *next_node = FindNodeByPin(link->_input_pin);
                    if (next_node != nullptr)
                        stack.emplace_back(next_node->_id);
                }
            }
        }
        return false;
    }

    void GraphDocument::MarkDirty()
    {
        _is_dirty = true;
    }
} // namespace Ailu
