#pragma once

#include "Graph/GraphDocument.h"
#include "UI/UIElement.h"

namespace Ailu
{
    namespace UI
    {
        class UIRenderer;
    }

    namespace Editor
    {
        enum class EGraphLinkRoute : u8
        {
            kPinBezier,
            kStateTransition
        };

        enum class EGraphPinPresentation : u8
        {
            kFull,
            kDotOnly,
            kHoverOnly
        };

        struct GraphCanvasPresentation
        {
            EGraphLinkRoute _link_route = EGraphLinkRoute::kPinBezier;
            EGraphPinPresentation _pin_presentation = EGraphPinPresentation::kFull;
            bool _draw_direction_arrow = false;
            bool _separate_bidirectional_links = false;
            bool _node_as_link_target = false;
            bool _allow_reroute = true;
        };

        struct GraphLinkGeometry
        {
            Vector2f _start = Vector2f::kZero;
            Vector2f _control0 = Vector2f::kZero;
            Vector2f _control1 = Vector2f::kZero;
            Vector2f _end = Vector2f::kZero;
        };

        struct GraphEditorStyle
        {
            Color _background_color = Color(0.035f, 0.038f, 0.045f, 1.0f);
            Color _minor_grid_color = Color(0.18f, 0.19f, 0.22f, 0.28f);
            Color _major_grid_color = Color(0.28f, 0.30f, 0.34f, 0.45f);
            Color _node_background_color = Color(0.115f, 0.12f, 0.135f, 1.0f);
            Color _hovered_node_background_color = Color(0.15f, 0.155f, 0.17f, 1.0f);
            Color _selected_node_background_color = Color(0.13f, 0.18f, 0.22f, 1.0f);
            Color _node_title_text_color = Color(0.92f, 0.94f, 0.96f, 1.0f);
            Color _pin_text_color = Color(0.78f, 0.81f, 0.86f, 1.0f);
            Color _exec_pin_color = Color(0.95f, 0.95f, 0.95f, 1.0f);
            Color _value_pin_color = Color(0.12f, 0.62f, 0.95f, 1.0f);
            Color _link_color = Color(0.76f, 0.78f, 0.82f, 1.0f);
            Color _selected_link_color = Color(0.1f, 0.55f, 0.95f, 1.0f);
            Color _invalid_link_color = Color(0.95f, 0.18f, 0.14f, 1.0f);
            Color _compatible_pin_color = Color(0.25f, 0.9f, 0.45f, 1.0f);
            Color _incompatible_pin_color = Color(0.95f, 0.26f, 0.2f, 1.0f);
            Color _marquee_fill_color = Color(0.1f, 0.55f, 0.95f, 0.16f);
            Color _marquee_border_color = Color(0.38f, 0.74f, 1.0f, 0.82f);
            Color _message_text_color = Color(1.0f, 0.64f, 0.56f, 1.0f);
            Color _alignment_guide_color = Color(0.36f, 0.86f, 1.0f, 0.78f);
            f32 _node_corner_radius = 6.0f;
            f32 _link_thickness = 2.0f;
            f32 _selected_link_thickness = 4.0f;
            f32 _pin_radius = 5.0f;
            f32 _canvas_corner_radius = 6.0f;
            f32 _title_height = 28.0f;
            f32 _pin_row_height = 22.0f;
        };

        class GraphCanvas : public UI::UIElement
        {
        public:
            GraphCanvas();
            explicit GraphCanvas(GraphDocument *document);

            void SetDocument(GraphDocument *document);
            void SetPresentation(const GraphCanvasPresentation &presentation);
            GraphDocument *Document() const { return _document; }
            void SetView(Vector2f view_offset, f32 zoom);
            Vector2f ViewOffset() const { return _view_offset; }
            f32 Zoom() const { return _zoom; }

            Vector2f ScreenToGraph(Vector2f screen_pos) const;
            Vector2f GraphToScreen(Vector2f graph_pos) const;
            Vector4f GraphRectToScreen(Vector4f graph_rect) const;
            Vector<const GraphNodeData *> GetVisibleNodes() const;
            Vector2f GetPinGraphPosition(const GraphNodeData &node, const GraphPinData &pin) const;
            Vector2f GetPinScreenPosition(const GraphNodeData &node, const GraphPinData &pin) const;
            const Vector<Guid> &SelectedNodes() const { return _selected_nodes; }
            const Vector<Guid> &SelectedLinks() const { return _selected_links; }
            const Vector<Guid> &SelectedComments() const { return _selected_comments; }
            void ClearSelection();
            bool FocusNode(const Guid &node_id, bool select = true);
            bool FocusLink(const Guid &link_id, bool select = true);
            bool SearchAndFocusNode(StringView search_text);
            void FocusSelection();
            void FocusAll();

            Vector2f MeasureDesiredSize() override;
            void Update(f32 dt) override;
            UI::UIElement *HitTest(Vector2f pos) override;

        protected:
            void RenderImpl(UI::UIRenderer &renderer) override;

        private:
            Vector2f GetNodeDisplaySize(const GraphNodeData &node) const;
            bool IsRerouteNode(const GraphNodeData &node) const;
            Color GetNodeTitleColor(const GraphNodeData &node) const;
            Color GetPinColor(const GraphPinData &pin) const;
            void DrawBackground(UI::UIRenderer &renderer, const Vector4f &content_rect);
            void DrawGrid(UI::UIRenderer &renderer, const Vector4f &content_rect);
            void DrawComments(UI::UIRenderer &renderer);
            void DrawLinks(UI::UIRenderer &renderer);
            void DrawLink(UI::UIRenderer &renderer, const GraphLinkData &link);
            void DrawArrow(UI::UIRenderer &renderer, const GraphLinkGeometry &geometry, Color color);
            void DrawPendingLink(UI::UIRenderer &renderer);
            void DrawNodes(UI::UIRenderer &renderer, const Vector<const GraphNodeData *> &visible_nodes);
            void DrawNode(UI::UIRenderer &renderer, const GraphNodeData &node);
            void DrawPin(UI::UIRenderer &renderer, const GraphNodeData &node, const GraphPinData &pin);
            void DrawAlignmentGuides(UI::UIRenderer &renderer, const Vector4f &content_rect);
            void DrawMiniMap(UI::UIRenderer &renderer, const Vector4f &content_rect);
            void DrawMarquee(UI::UIRenderer &renderer);
            void DrawConnectionMessage(UI::UIRenderer &renderer);
            bool NodeHasValidationMessage(const Guid &node_id) const;
            const GraphNodeData *HitTestNode(Vector2f screen_pos) const;
            const GraphCommentData *HitTestComment(Vector2f screen_pos) const;
            const GraphPinData *HitTestPin(Vector2f screen_pos) const;
            const GraphLinkData *HitTestLink(Vector2f screen_pos) const;
            bool IsNodeSelected(const Guid &node_id) const;
            bool IsLinkSelected(const Guid &link_id) const;
            bool IsCommentSelected(const Guid &comment_id) const;
            void SelectNode(const Guid &node_id, bool append);
            void SelectLink(const Guid &link_id, bool append);
            void SelectComment(const Guid &comment_id, bool append);
            void ToggleNodeSelection(const Guid &node_id);
            void ToggleLinkSelection(const Guid &link_id);
            void ToggleCommentSelection(const Guid &comment_id);
            void BeginNodeDrag(Vector2f mouse_pos);
            void BeginCommentDrag(const Guid &comment_id, Vector2f mouse_pos);
            void UpdateNodeDrag(Vector2f mouse_pos);
            void BeginLinkDrag(const Guid &pin_id, Vector2f mouse_pos);
            void UpdateLinkDrag(Vector2f mouse_pos);
            void FinishLinkDrag(Vector2f mouse_pos);
            void InsertRerouteOnLink(const Guid &link_id, Vector2f screen_pos);
            void BeginMiniMapDrag(Vector2f mouse_pos);
            void UpdateMiniMapDrag(Vector2f mouse_pos);
            void OpenActionMenu(Vector2f screen_pos, Guid source_pin = Guid::EmptyGuid());
            void CreateNodeFromAction(const GraphNodeAction &action, Vector2f screen_pos, Guid source_pin);
            void ClearActionMenuPreview(Guid source_pin = Guid::EmptyGuid());
            void EndInteraction();
            void CancelInteraction();
            void BeginMarquee(Vector2f mouse_pos, bool append);
            void UpdateMarquee(Vector2f mouse_pos);
            void DeleteSelection();
            void CopySelection();
            void PasteClipboard(bool duplicate = false);
            void DuplicateSelection();
            void CreateCommentAroundSelection();
            void DisconnectPin(const Guid &pin_id);
            void FocusNodes(std::span<const Guid> node_ids);
            bool AutoScrollCanvas(Vector2f mouse_pos, f32 dt = 1.0f / 60.0f);
            Vector<GraphNodePosition> GetCurrentNodePositions(std::span<const GraphNodePosition> source) const;
            Vector<GraphCommentPosition> GetCurrentCommentPositions(std::span<const GraphCommentPosition> source) const;
            void CommitNodeDrag();
            bool TryGetMiniMapGeometry(Vector4f &mini_rect, Vector2f &mini_offset, f32 &mini_scale) const;
            bool HitTestMiniMap(Vector2f screen_pos) const;
            void CenterViewAtGraphPosition(Vector2f graph_position);
            Vector4f GetNodeGraphRect(const GraphNodeData &node) const;
            Vector4f GetMarqueeGraphRect() const;
            Vector2f SnapGraphPosition(Vector2f position) const;
            GraphLinkGeometry BuildLinkGeometry(const GraphLinkData &link) const;
            GraphConnectionResponse GetConnectionResponse(const Guid &target_pin_id) const;
            const GraphPinData *FindCompatiblePinOnNode(const GraphNodeData &node) const;
            bool ShouldDrawPin(const GraphNodeData &node, const GraphPinData &pin) const;
            bool IsPinCompatibleDragTarget(const Guid &pin_id) const;
            bool IsPinIncompatibleDragTarget(const Guid &pin_id) const;

            enum class EInteraction : u8
            {
                kNone,
                kPanning,
                kDraggingNodes,
                kDraggingLink,
                kMarquee,
                kDraggingMiniMap
            };

        private:
            GraphDocument *_document = nullptr;
            GraphCanvasPresentation _presentation;
            GraphEditorStyle _style;
            Vector2f _view_offset = Vector2f(40.0f, 40.0f);
            f32 _zoom = 1.0f;
            f32 _min_zoom = 0.25f;
            f32 _max_zoom = 2.0f;
            Guid _hovered_node = Guid::EmptyGuid();
            Guid _hovered_pin = Guid::EmptyGuid();
            Guid _hovered_link = Guid::EmptyGuid();
            Guid _hovered_comment = Guid::EmptyGuid();
            Guid _last_link_click = Guid::EmptyGuid();
            Vector<Guid> _selected_nodes;
            Vector<Guid> _selected_links;
            Vector<Guid> _selected_comments;
            Vector<Guid> _marquee_base_selection;
            Vector<GraphNodePosition> _drag_start_node_positions;
            Vector<GraphCommentPosition> _drag_start_comment_positions;
            Guid _drag_start_pin = Guid::EmptyGuid();
            Guid _drag_target_pin = Guid::EmptyGuid();
            Guid _menu_source_pin = Guid::EmptyGuid();
            String _connection_message;
            Vector2f _drag_start_mouse = Vector2f::kZero;
            Vector2f _interaction_mouse = Vector2f::kZero;
            Vector2f _last_link_click_mouse = Vector2f::kZero;
            Vector2f _menu_anchor_mouse = Vector2f::kZero;
            Vector2f _drag_start_view_offset = Vector2f::kZero;
            Vector4f _marquee_rect = Vector4f::kZero;
            EInteraction _interaction = EInteraction::kNone;
            bool _is_marquee_additive = false;
            bool _right_pan_moved = false;
            Vector2f _right_down_mouse = Vector2f::kZero;
            bool _snap_to_grid = true;
            f32 _grid_snap = 16.0f;
            f32 _last_link_click_timer = 1000.0f;
        };
    } // namespace Editor
} // namespace Ailu
