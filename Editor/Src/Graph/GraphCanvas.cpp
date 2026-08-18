#include "Graph/GraphCanvas.h"
#include "Framework/Common/Input.h"
#include "Graph/GraphActionMenu.h"
#include "Graph/GraphNodeRegistry.h"
#include "UI/Style/UIStyleBasic.h"
#include "UI/UIRenderer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <unordered_map>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kMinorGridSpacing = 16.0f;
            constexpr u32 kMajorGridFrequency = 5u;
            constexpr f32 kNodeTextZoomThreshold = 0.45f;
            constexpr f32 kFullDetailZoomThreshold = 0.75f;
            constexpr f32 kAutoScrollEdgeSize = 26.0f;
            constexpr f32 kAutoScrollSpeed = 720.0f;

            UI::UIBrush MakeColorBrush(Color color)
            {
                UI::UIBrush brush;
                brush._type = UI::EUIBrushType::kColor;
                brush._tint = color;
                return brush;
            }

            bool RectIntersects(const Vector4f &lhs, const Vector4f &rhs)
            {
                return lhs.x < rhs.x + rhs.z && lhs.x + lhs.z > rhs.x && lhs.y < rhs.y + rhs.w &&
                       lhs.y + lhs.w > rhs.y;
            }

            bool RectContains(const Vector4f &rect, Vector2f point)
            {
                return point.x >= rect.x && point.x <= rect.x + rect.z && point.y >= rect.y &&
                       point.y <= rect.y + rect.w;
            }

            String ToLowerCopy(String value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            }

            bool ContainsSearchText(const String &value, const String &search_text)
            {
                return ToLowerCopy(value).find(search_text) != String::npos;
            }

            Vector4f MakePositiveRect(Vector2f start, Vector2f end)
            {
                const f32 x = std::min(start.x, end.x);
                const f32 y = std::min(start.y, end.y);
                return {x, y, std::abs(end.x - start.x), std::abs(end.y - start.y)};
            }

            Vector2f SampleBezier(Vector2f start, Vector2f start_tangent, Vector2f end_tangent, Vector2f end, f32 t)
            {
                const f32 inv_t = 1.0f - t;
                return start * (inv_t * inv_t * inv_t) + start_tangent * (3.0f * inv_t * inv_t * t) +
                       end_tangent * (3.0f * inv_t * t * t) + end * (t * t * t);
            }

            void GetLinkTangents(Vector2f start, Vector2f end, Vector2f &start_tangent, Vector2f &end_tangent)
            {
                const f32 distance_x = std::abs(end.x - start.x);
                const f32 distance_y = std::abs(end.y - start.y);
                const f32 tangent = std::clamp(std::max(distance_x * 0.45f, distance_y * 0.18f), 28.0f, 160.0f);
                start_tangent = start + Vector2f(tangent, 0.0f);
                end_tangent = end - Vector2f(tangent, 0.0f);
            }

            f32 DistanceToSegmentSquared(Vector2f point, Vector2f start, Vector2f end)
            {
                const Vector2f segment = end - start;
                const f32 segment_len_sq = segment.x * segment.x + segment.y * segment.y;
                if (segment_len_sq <= 0.0001f)
                {
                    const Vector2f delta = point - start;
                    return delta.x * delta.x + delta.y * delta.y;
                }
                const Vector2f point_delta = point - start;
                const f32 t = std::clamp((point_delta.x * segment.x + point_delta.y * segment.y) / segment_len_sq,
                                         0.0f, 1.0f);
                const Vector2f closest = start + segment * t;
                const Vector2f delta = point - closest;
                return delta.x * delta.x + delta.y * delta.y;
            }

            void AdjustVerticalLineForRoundedRect(f32 x, const Vector4f &rect, f32 radius, f32 &top, f32 &bottom)
            {
                if (radius <= 0.0f)
                    return;

                auto inset = [radius](f32 distance_from_corner_edge)
                {
                    const f32 clamped_distance = std::clamp(distance_from_corner_edge, 0.0f, radius);
                    const f32 dx = radius - clamped_distance;
                    return radius - std::sqrt(std::max(0.0f, radius * radius - dx * dx));
                };

                const f32 left_distance = x - rect.x;
                const f32 right_distance = rect.x + rect.z - x;
                if (left_distance >= 0.0f && left_distance < radius)
                {
                    const f32 corner_inset = inset(left_distance);
                    top = std::max(top, rect.y + corner_inset);
                    bottom = std::min(bottom, rect.y + rect.w - corner_inset);
                }
                if (right_distance >= 0.0f && right_distance < radius)
                {
                    const f32 corner_inset = inset(right_distance);
                    top = std::max(top, rect.y + corner_inset);
                    bottom = std::min(bottom, rect.y + rect.w - corner_inset);
                }
            }

            void AdjustHorizontalLineForRoundedRect(f32 y, const Vector4f &rect, f32 radius, f32 &left, f32 &right)
            {
                if (radius <= 0.0f)
                    return;

                auto inset = [radius](f32 distance_from_corner_edge)
                {
                    const f32 clamped_distance = std::clamp(distance_from_corner_edge, 0.0f, radius);
                    const f32 dy = radius - clamped_distance;
                    return radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
                };

                const f32 top_distance = y - rect.y;
                const f32 bottom_distance = rect.y + rect.w - y;
                if (top_distance >= 0.0f && top_distance < radius)
                {
                    const f32 corner_inset = inset(top_distance);
                    left = std::max(left, rect.x + corner_inset);
                    right = std::min(right, rect.x + rect.z - corner_inset);
                }
                if (bottom_distance >= 0.0f && bottom_distance < radius)
                {
                    const f32 corner_inset = inset(bottom_distance);
                    left = std::max(left, rect.x + corner_inset);
                    right = std::min(right, rect.x + rect.z - corner_inset);
                }
            }

            struct GraphClipboardPayload
            {
                Vector<GraphNodeData> _nodes;
                Vector<GraphLinkData> _links;
                Vector<GraphCommentData> _comments;
                bool _has_data = false;
            };

            static GraphClipboardPayload s_graph_clipboard;
        } // namespace

        GraphCanvas::GraphCanvas() : UIElement("GraphCanvas")
        {
            SetWantsMouseEvents(true);
            SetInteractiveEnabled(true);

            OnMouseDown() += [this](UI::UIEvent &e)
            {
                if (_document == nullptr)
                    return;

                RequestFocus();
                _interaction_mouse = e._mouse_position;
                if (e._key_code == EKey::kRBUTTON)
                {
                    _interaction = EInteraction::kPanning;
                    _drag_start_mouse = e._mouse_position;
                    _drag_start_view_offset = _view_offset;
                    _right_down_mouse = e._mouse_position;
                    _right_pan_moved = false;
                    e._is_handled = true;
                    return;
                }

                if (e._key_code != EKey::kLBUTTON)
                    return;

                const bool is_ctrl_down = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL) ||
                                          Input::IsKeyDown(EKey::kCONTROL);
                if (HitTestMiniMap(e._mouse_position))
                {
                    BeginMiniMapDrag(e._mouse_position);
                    e._is_handled = true;
                    return;
                }

                const GraphNodeData *hit_node = HitTestNode(e._mouse_position);
                if (hit_node != nullptr && IsRerouteNode(*hit_node))
                {
                    if (is_ctrl_down)
                        ToggleNodeSelection(hit_node->_id);
                    else if (!IsNodeSelected(hit_node->_id))
                        SelectNode(hit_node->_id, false);
                    if (IsNodeSelected(hit_node->_id))
                        BeginNodeDrag(e._mouse_position);
                    e._is_handled = true;
                    return;
                }

                const GraphPinData *hit_pin = HitTestPin(e._mouse_position);
                if (hit_pin != nullptr)
                {
                    if (Input::IsKeyDown(EKey::kMENU) || Input::IsKeyDown(EKey::kALT))
                    {
                        DisconnectPin(hit_pin->_id);
                    }
                    else
                    {
                        if (!is_ctrl_down)
                            ClearSelection();
                        BeginLinkDrag(hit_pin->_id, e._mouse_position);
                    }
                    e._is_handled = true;
                    return;
                }

                if (hit_node != nullptr)
                {
                    if (is_ctrl_down)
                        ToggleNodeSelection(hit_node->_id);
                    else if (!IsNodeSelected(hit_node->_id))
                        SelectNode(hit_node->_id, false);
                    if (IsNodeSelected(hit_node->_id))
                        BeginNodeDrag(e._mouse_position);
                }
                else
                {
                    const GraphLinkData *hit_link = HitTestLink(e._mouse_position);
                    if (hit_link != nullptr)
                    {
                        constexpr f32 kLinkDoubleClickSeconds = 0.45f;
                        constexpr f32 kLinkDoubleClickDistance = 12.0f;
                        const Vector2f click_delta = e._mouse_position - _last_link_click_mouse;
                        if (_last_link_click == hit_link->_id && _last_link_click_timer <= kLinkDoubleClickSeconds &&
                            click_delta.x * click_delta.x + click_delta.y * click_delta.y <=
                                    kLinkDoubleClickDistance * kLinkDoubleClickDistance)
                        {
                            InsertRerouteOnLink(hit_link->_id, e._mouse_position);
                            _last_link_click = Guid::EmptyGuid();
                            _last_link_click_timer = 1000.0f;
                            e._is_handled = true;
                            return;
                        }
                        _last_link_click = hit_link->_id;
                        _last_link_click_mouse = e._mouse_position;
                        _last_link_click_timer = 0.0f;
                        if (is_ctrl_down)
                            ToggleLinkSelection(hit_link->_id);
                        else
                            SelectLink(hit_link->_id, false);
                    }
                    else
                    {
                        const GraphCommentData *hit_comment = HitTestComment(e._mouse_position);
                        if (hit_comment != nullptr)
                        {
                            if (is_ctrl_down)
                                ToggleCommentSelection(hit_comment->_id);
                            else if (!IsCommentSelected(hit_comment->_id))
                                SelectComment(hit_comment->_id, false);
                            if (IsCommentSelected(hit_comment->_id))
                                BeginCommentDrag(hit_comment->_id, e._mouse_position);
                        }
                        else
                        {
                            BeginMarquee(e._mouse_position, is_ctrl_down);
                            if (!is_ctrl_down)
                                ClearSelection();
                        }
                    }
                }

                e._is_handled = true;
            };

            OnMouseMove() += [this](UI::UIEvent &e)
            {
                if (_document == nullptr)
                    return;

                _interaction_mouse = e._mouse_position;
                if (_interaction == EInteraction::kPanning)
                {
                    const Vector2f right_delta = e._mouse_position - _right_down_mouse;
                    _right_pan_moved = _right_pan_moved || right_delta.x * right_delta.x + right_delta.y * right_delta.y > 16.0f;
                    _view_offset = _drag_start_view_offset + e._mouse_position - _drag_start_mouse;
                    InvalidatePaint();
                    e._is_handled = true;
                    return;
                }
                if (_interaction == EInteraction::kDraggingNodes)
                {
                    UpdateNodeDrag(e._mouse_position);
                    e._is_handled = true;
                    return;
                }
                if (_interaction == EInteraction::kDraggingLink)
                {
                    UpdateLinkDrag(e._mouse_position);
                    e._is_handled = true;
                    return;
                }
                if (_interaction == EInteraction::kMarquee)
                {
                    UpdateMarquee(e._mouse_position);
                    e._is_handled = true;
                    return;
                }
                if (_interaction == EInteraction::kDraggingMiniMap)
                {
                    UpdateMiniMapDrag(e._mouse_position);
                    e._is_handled = true;
                    return;
                }

                const GraphNodeData *node = HitTestNode(e._mouse_position);
                const GraphPinData *pin = HitTestPin(e._mouse_position);
                const GraphLinkData *link = node == nullptr && pin == nullptr ? HitTestLink(e._mouse_position) :
                                                                                 nullptr;
                const GraphCommentData *comment = node == nullptr && pin == nullptr && link == nullptr ?
                                                  HitTestComment(e._mouse_position) : nullptr;
                const Guid hovered_node = node != nullptr ? node->_id : Guid::EmptyGuid();
                const Guid hovered_pin = pin != nullptr ? pin->_id : Guid::EmptyGuid();
                const Guid hovered_link = link != nullptr ? link->_id : Guid::EmptyGuid();
                const Guid hovered_comment = comment != nullptr ? comment->_id : Guid::EmptyGuid();
                if (!(_hovered_node == hovered_node) || !(_hovered_pin == hovered_pin) ||
                    !(_hovered_link == hovered_link) || !(_hovered_comment == hovered_comment))
                {
                    _hovered_node = hovered_node;
                    _hovered_pin = hovered_pin;
                    _hovered_link = hovered_link;
                    _hovered_comment = hovered_comment;
                    InvalidatePaint();
                }
            };

            OnMouseUp() += [this](UI::UIEvent &e)
            {
                if (e._key_code == EKey::kRBUTTON && _interaction == EInteraction::kPanning)
                {
                    const bool is_click = !_right_pan_moved;
                    const Vector2f menu_position = e._mouse_position;
                    const GraphPinData *hit_pin = is_click ? HitTestPin(_right_down_mouse) : nullptr;
                    EndInteraction();
                    if (is_click)
                        OpenActionMenu(menu_position, hit_pin != nullptr ? hit_pin->_id : Guid::EmptyGuid());
                    e._is_handled = true;
                    return;
                }
                if (e._key_code == EKey::kLBUTTON && _interaction == EInteraction::kDraggingLink)
                {
                    FinishLinkDrag(e._mouse_position);
                    e._is_handled = true;
                    return;
                }
                if (e._key_code == EKey::kLBUTTON)
                {
                    EndInteraction();
                    e._is_handled = true;
                }
            };

            OnMouseScroll() += [this](UI::UIEvent &e)
            {
                const f32 old_zoom = _zoom;
                if (e._scroll_delta > 0.0f)
                    _zoom = std::min(_zoom * 1.1f, _max_zoom);
                else if (e._scroll_delta < 0.0f)
                    _zoom = std::max(_zoom / 1.1f, _min_zoom);

                if (!NearbyEqual(old_zoom, _zoom))
                {
                    const Vector4f content_rect = GetContentRect();
                    const Vector2f content_mouse = e._mouse_position - content_rect.xy;
                    const f32 zoom_ratio = _zoom / old_zoom;
                    _view_offset = content_mouse - (content_mouse - _view_offset) * zoom_ratio;
                    InvalidatePaint();
                }
                e._is_handled = true;
            };

            OnKeyDown() += [this](UI::UIEvent &e)
            {
                if (_document == nullptr)
                    return;

                const bool is_ctrl_down = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL) ||
                                          Input::IsKeyDown(EKey::kCONTROL);
                const bool is_shift_down = Input::IsKeyDown(EKey::kLSHIFT) || Input::IsKeyDown(EKey::kRSHIFT) ||
                                           Input::IsKeyDown(EKey::kSHIFT);
                if (is_ctrl_down && e._key_code == EKey::kZ)
                {
                    is_shift_down ? _document->Commands().Redo() : _document->Commands().Undo();
                    InvalidatePaint();
                    e._is_handled = true;
                }
                else if (is_ctrl_down && e._key_code == EKey::kY)
                {
                    _document->Commands().Redo();
                    InvalidatePaint();
                    e._is_handled = true;
                }
                else if (is_ctrl_down && e._key_code == EKey::kC)
                {
                    CopySelection();
                    e._is_handled = true;
                }
                else if (is_ctrl_down && e._key_code == EKey::kV)
                {
                    PasteClipboard();
                    e._is_handled = true;
                }
                else if (is_ctrl_down && e._key_code == EKey::kD)
                {
                    DuplicateSelection();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kC && !is_ctrl_down)
                {
                    CreateCommentAroundSelection();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kDELETE)
                {
                    DeleteSelection();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kF)
                {
                    FocusSelection();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kHOME)
                {
                    FocusAll();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kESCAPE)
                {
                    CancelInteraction();
                    e._is_handled = true;
                }
                else if (e._key_code == EKey::kSPACE)
                {
                    OpenActionMenu(_interaction_mouse);
                    e._is_handled = true;
                }
            };
        }

        GraphCanvas::GraphCanvas(GraphDocument *document) : GraphCanvas()
        {
            SetDocument(document);
        }

        void GraphCanvas::SetDocument(GraphDocument *document)
        {
            if (_document == document)
                return;
            CancelInteraction();
            ClearSelection();
            _document = document;
            InvalidatePaint();
        }

        void GraphCanvas::SetView(Vector2f view_offset, f32 zoom)
        {
            _view_offset = view_offset;
            _zoom = std::clamp(zoom, _min_zoom, _max_zoom);
            InvalidatePaint();
        }

        Vector2f GraphCanvas::ScreenToGraph(Vector2f screen_pos) const
        {
            const Vector4f content_rect = GetContentRect();
            return (screen_pos - content_rect.xy - _view_offset) / _zoom;
        }

        Vector2f GraphCanvas::GraphToScreen(Vector2f graph_pos) const
        {
            const Vector4f content_rect = GetContentRect();
            return content_rect.xy + _view_offset + graph_pos * _zoom;
        }

        Vector4f GraphCanvas::GraphRectToScreen(Vector4f graph_rect) const
        {
            const Vector2f screen_pos = GraphToScreen(graph_rect.xy);
            return Vector4f(screen_pos.x, screen_pos.y, graph_rect.z * _zoom, graph_rect.w * _zoom);
        }

        Vector<const GraphNodeData *> GraphCanvas::GetVisibleNodes() const
        {
            Vector<const GraphNodeData *> visible_nodes;
            if (_document == nullptr)
                return visible_nodes;

            const Vector4f content_rect = GetContentRect();
            for (const GraphNodeData &node : _document->Nodes())
            {
                const Vector2f node_size = GetNodeDisplaySize(node);
                const Vector4f node_rect = GraphRectToScreen({node._position.x, node._position.y, node_size.x,
                                                              node_size.y});
                if (RectIntersects(node_rect, content_rect))
                    visible_nodes.emplace_back(&node);
            }
            return visible_nodes;
        }

        Vector2f GraphCanvas::GetPinGraphPosition(const GraphNodeData &node, const GraphPinData &pin) const
        {
            u32 visible_pin_index = 0u;
            for (const GraphPinData &candidate : node._pins)
            {
                if (candidate._is_hidden || candidate._direction != pin._direction)
                    continue;
                if (candidate._id == pin._id)
                    break;
                ++visible_pin_index;
            }

            const Vector2f node_size = GetNodeDisplaySize(node);
            if (IsRerouteNode(node))
            {
                const f32 y = node._position.y + node_size.y * 0.5f;
                return {pin._direction == EGraphPinDirection::kInput ? node._position.x :
                                                                       node._position.x + node_size.x, y};
            }
            const f32 x = pin._direction == EGraphPinDirection::kInput ? node._position.x :
                                                                             node._position.x + node_size.x;
            const f32 y = node._position.y + _style._title_height + (static_cast<f32>(visible_pin_index) + 0.5f) *
                          _style._pin_row_height;
            return {x, y};
        }

        Vector2f GraphCanvas::GetPinScreenPosition(const GraphNodeData &node, const GraphPinData &pin) const
        {
            return GraphToScreen(GetPinGraphPosition(node, pin));
        }

        Vector2f GraphCanvas::MeasureDesiredSize()
        {
            return {500.0f, 360.0f};
        }

        void GraphCanvas::Update(f32 dt)
        {
            UIElement::Update(dt);
            _last_link_click_timer += dt;
            if (_interaction == EInteraction::kDraggingNodes || _interaction == EInteraction::kDraggingLink ||
                _interaction == EInteraction::kMarquee)
            {
                if (AutoScrollCanvas(_interaction_mouse, dt))
                {
                    if (_interaction == EInteraction::kDraggingNodes)
                        UpdateNodeDrag(_interaction_mouse);
                    else if (_interaction == EInteraction::kDraggingLink)
                        UpdateLinkDrag(_interaction_mouse);
                    else if (_interaction == EInteraction::kMarquee)
                        UpdateMarquee(_interaction_mouse);
                }
            }
        }

        UI::UIElement *GraphCanvas::HitTest(Vector2f pos)
        {
            return IsPointInside(pos, GetContentRect()) ? this : nullptr;
        }

        void GraphCanvas::RenderImpl(UI::UIRenderer &renderer)
        {
            const Vector4f content_rect = GetContentRect();
            if (content_rect.z <= 0.0f || content_rect.w <= 0.0f)
                return;

            renderer.PushScissor(content_rect);
                DrawBackground(renderer, content_rect);
                DrawGrid(renderer, content_rect);
            if (_document != nullptr)
            {
                DrawComments(renderer);
                DrawLinks(renderer);
                DrawPendingLink(renderer);
                DrawNodes(renderer, GetVisibleNodes());
                DrawAlignmentGuides(renderer, content_rect);
                DrawMiniMap(renderer, content_rect);
                DrawMarquee(renderer);
                DrawConnectionMessage(renderer);
            }
            renderer.PopScissor();
        }

        void GraphCanvas::ClearSelection()
        {
            if (_selected_nodes.empty() && _selected_links.empty() && _selected_comments.empty())
                return;
            _selected_nodes.clear();
            _selected_links.clear();
            _selected_comments.clear();
            InvalidatePaint();
        }

        Vector2f GraphCanvas::GetNodeDisplaySize(const GraphNodeData &node) const
        {
            if (IsRerouteNode(node))
                return {18.0f, 18.0f};

            u32 input_count = 0u;
            u32 output_count = 0u;
            for (const GraphPinData &pin : node._pins)
            {
                if (pin._is_hidden)
                    continue;
                pin._direction == EGraphPinDirection::kInput ? ++input_count : ++output_count;
            }

            const f32 needed_height = _style._title_height + static_cast<f32>(std::max(input_count, output_count)) *
                                      _style._pin_row_height + 8.0f;
            return {std::max(node._size.x, 140.0f), std::max(node._size.y, needed_height)};
        }

        bool GraphCanvas::IsRerouteNode(const GraphNodeData &node) const
        {
            return node._node_type == "Flow.Reroute";
        }

        Color GraphCanvas::GetNodeTitleColor(const GraphNodeData &node) const
        {
            const GraphNodeDesc *desc = GraphNodeRegistry::Get().FindNode(node._node_type);
            return desc != nullptr ? desc->_title_color : Color(0.42f, 0.17f, 0.16f, 1.0f);
        }

        Color GraphCanvas::GetPinColor(const GraphPinData &pin) const
        {
            return pin._kind == EGraphPinKind::kExecution ? _style._exec_pin_color : _style._value_pin_color;
        }

        void GraphCanvas::DrawBackground(UI::UIRenderer &renderer, const Vector4f &content_rect)
        {
            renderer.DrawQuad(content_rect, MakeColorBrush(_style._background_color),
                              Vector4f(_style._canvas_corner_radius), -0.3f);
        }

        void GraphCanvas::DrawGrid(UI::UIRenderer &renderer, const Vector4f &content_rect)
        {
            const f32 grid_spacing = kMinorGridSpacing * _zoom;
            if (grid_spacing < 3.0f)
                return;

            const Vector2f top_left_graph = ScreenToGraph(content_rect.xy);
            const f32 start_x = std::floor(top_left_graph.x / kMinorGridSpacing) * kMinorGridSpacing;
            const f32 start_y = std::floor(top_left_graph.y / kMinorGridSpacing) * kMinorGridSpacing;
            const u32 line_count_x = static_cast<u32>(std::ceil(content_rect.z / grid_spacing)) + 2u;
            const u32 line_count_y = static_cast<u32>(std::ceil(content_rect.w / grid_spacing)) + 2u;

            for (u32 index = 0u; index < line_count_x; ++index)
            {
                const f32 graph_x = start_x + static_cast<f32>(index) * kMinorGridSpacing;
                const Vector2f screen = GraphToScreen({graph_x, 0.0f});
                const i32 grid_index = static_cast<i32>(std::round(graph_x / kMinorGridSpacing));
                const bool is_major = grid_index % static_cast<i32>(kMajorGridFrequency) == 0;
                f32 top = content_rect.y;
                f32 bottom = content_rect.y + content_rect.w;
                AdjustVerticalLineForRoundedRect(screen.x, content_rect, _style._canvas_corner_radius, top, bottom);
                renderer.DrawLine({screen.x, top}, {screen.x, bottom}, 1.0f,
                                  is_major ? _style._major_grid_color : _style._minor_grid_color, -0.2f);
            }

            for (u32 index = 0u; index < line_count_y; ++index)
            {
                const f32 graph_y = start_y + static_cast<f32>(index) * kMinorGridSpacing;
                const Vector2f screen = GraphToScreen({0.0f, graph_y});
                const i32 grid_index = static_cast<i32>(std::round(graph_y / kMinorGridSpacing));
                const bool is_major = grid_index % static_cast<i32>(kMajorGridFrequency) == 0;
                f32 left = content_rect.x;
                f32 right = content_rect.x + content_rect.z;
                AdjustHorizontalLineForRoundedRect(screen.y, content_rect, _style._canvas_corner_radius, left, right);
                renderer.DrawLine({left, screen.y}, {right, screen.y}, 1.0f,
                                  is_major ? _style._major_grid_color : _style._minor_grid_color, -0.2f);
            }
        }

        void GraphCanvas::DrawComments(UI::UIRenderer &renderer)
        {
            if (_document == nullptr || _document->Comments().empty())
                return;

            const Vector4f content_rect = GetContentRect();
            for (const GraphCommentData &comment : _document->Comments())
            {
                const Vector4f comment_rect = GraphRectToScreen({comment._position.x, comment._position.y,
                                                                 comment._size.x, comment._size.y});
                if (!RectIntersects(comment_rect, content_rect))
                    continue;

                Color fill = comment._color;
                const bool is_selected = IsCommentSelected(comment._id);
                fill.w = std::clamp(fill.w + (is_selected ? 0.12f : 0.0f), 0.08f, 0.46f);
                renderer.DrawQuad(comment_rect, MakeColorBrush(fill), Vector4f(5.0f * _zoom), 0.0f);
                renderer.DrawBox(comment_rect.xy, comment_rect.zw, is_selected ? 2.0f : 1.0f,
                                 is_selected ? Color(0.36f, 0.86f, 1.0f, 0.92f) :
                                               Color(fill.x, fill.y, fill.z, 0.58f), 0.01f);
                if (_zoom >= kNodeTextZoomThreshold)
                {
                    renderer.DrawText(comment._title, comment_rect.xy + Vector2f(10.0f * _zoom, 7.0f * _zoom),
                                      13.0f * _zoom, Color(0.88f, 0.92f, 0.96f, 0.95f));
                }
            }
        }

        void GraphCanvas::DrawLinks(UI::UIRenderer &renderer)
        {
            for (const GraphLinkData &link : _document->Links())
                DrawLink(renderer, link);
        }

        void GraphCanvas::DrawLink(UI::UIRenderer &renderer, const GraphLinkData &link)
        {
            const GraphNodeData *output_node = _document->FindNodeByPin(link._output_pin);
            const GraphNodeData *input_node = _document->FindNodeByPin(link._input_pin);
            const GraphPinData *output_pin = _document->FindPin(link._output_pin);
            const GraphPinData *input_pin = _document->FindPin(link._input_pin);
            if (output_node == nullptr || input_node == nullptr || output_pin == nullptr || input_pin == nullptr)
                return;

            const Vector2f start = GetPinScreenPosition(*output_node, *output_pin);
            const Vector2f end = GetPinScreenPosition(*input_node, *input_pin);
            const Vector4f content_rect = GetContentRect();
            const Vector4f link_bounds = {std::min(start.x, end.x), std::min(start.y, end.y),
                                          std::abs(end.x - start.x), std::abs(end.y - start.y)};
            if (!RectIntersects({link_bounds.x - 80.0f, link_bounds.y - 80.0f, link_bounds.z + 160.0f,
                                link_bounds.w + 160.0f}, content_rect))
                return;
            const bool is_selected = IsLinkSelected(link._id);
            const bool is_hovered = _hovered_link == link._id;
            const f32 thickness = is_selected || is_hovered ? _style._selected_link_thickness : _style._link_thickness;
            const Color color = is_selected || is_hovered ? _style._selected_link_color : _style._link_color;
            const u32 segments = _zoom < kNodeTextZoomThreshold ? 8u : (_zoom < kFullDetailZoomThreshold ? 14u : 24u);
            Vector2f start_tangent = Vector2f::kZero;
            Vector2f end_tangent = Vector2f::kZero;
            GetLinkTangents(start, end, start_tangent, end_tangent);
            renderer.DrawBezier(start, start_tangent, end_tangent, end, thickness, color, 0.05f, segments);
        }

        void GraphCanvas::DrawPendingLink(UI::UIRenderer &renderer)
        {
            if (_document == nullptr)
                return;

            const bool is_dragging_link = _interaction == EInteraction::kDraggingLink;
            const Guid source_pin = is_dragging_link ? _drag_start_pin : _menu_source_pin;
            if (source_pin == Guid::EmptyGuid())
                return;

            const GraphNodeData *node = _document->FindNodeByPin(source_pin);
            const GraphPinData *pin = _document->FindPin(source_pin);
            if (node == nullptr || pin == nullptr)
                return;

            const Vector2f anchor_pos = is_dragging_link ? _interaction_mouse : _menu_anchor_mouse;
            const Vector2f pin_pos = GetPinScreenPosition(*node, *pin);
            const Vector2f start = pin->_direction == EGraphPinDirection::kOutput ? pin_pos : anchor_pos;
            const Vector2f end = pin->_direction == EGraphPinDirection::kOutput ? anchor_pos : pin_pos;
            Color color = _style._link_color;
            if (is_dragging_link && _drag_target_pin != Guid::EmptyGuid())
            {
                const GraphConnectionResponse response = GetConnectionResponse(_drag_target_pin);
                color = response._action == EGraphConnectionAction::kDisallow ? _style._invalid_link_color :
                                                                                _style._compatible_pin_color;
            }
            Vector2f start_tangent = Vector2f::kZero;
            Vector2f end_tangent = Vector2f::kZero;
            GetLinkTangents(start, end, start_tangent, end_tangent);
            renderer.DrawBezier(start, start_tangent, end_tangent, end, _style._link_thickness, color, 0.08f, 24u);
        }

        void GraphCanvas::DrawNodes(UI::UIRenderer &renderer, const Vector<const GraphNodeData *> &visible_nodes)
        {
            for (const GraphNodeData *node : visible_nodes)
                DrawNode(renderer, *node);
        }

        void GraphCanvas::DrawNode(UI::UIRenderer &renderer, const GraphNodeData &node)
        {
            const Vector2f node_size = GetNodeDisplaySize(node);
            const Vector4f node_rect = GraphRectToScreen({node._position.x, node._position.y, node_size.x,
                                                          node_size.y});
            const bool is_selected = IsNodeSelected(node._id);
            const bool is_hovered = _hovered_node == node._id;
            if (IsRerouteNode(node))
            {
                const f32 radius = std::max(5.0f, node_rect.z * 0.5f);
                const Color color = is_selected ? _style._selected_link_color :
                                    is_hovered ? Color(0.85f, 0.9f, 0.96f, 1.0f) : _style._exec_pin_color;
                renderer.DrawQuad(node_rect, MakeColorBrush(color), Vector4f(radius), 0.16f);
                return;
            }

            const Vector4f title_rect = {node_rect.x, node_rect.y, node_rect.z, _style._title_height * _zoom};
            const Vector4f corner_radius = Vector4f(_style._node_corner_radius * _zoom);
            const Color background_color = is_selected ? _style._selected_node_background_color :
                                           is_hovered ? _style._hovered_node_background_color :
                                                        _style._node_background_color;
            Color title_color = GetNodeTitleColor(node);
            if (is_selected)
            {
                title_color.x = std::min(title_color.x + 0.08f, 1.0f);
                title_color.y = std::min(title_color.y + 0.08f, 1.0f);
                title_color.z = std::min(title_color.z + 0.08f, 1.0f);
            }

            renderer.DrawQuad(node_rect, MakeColorBrush(background_color), corner_radius, 0.1f);
            renderer.DrawQuad(title_rect, MakeColorBrush(title_color), {corner_radius.x, corner_radius.y, 0.0f, 0.0f},
                              0.12f);

            if (NodeHasValidationMessage(node._id))
            {
                const f32 marker_size = std::max(10.0f, 15.0f * _zoom);
                const Vector4f marker_rect = {node_rect.x + node_rect.z - marker_size - 6.0f * _zoom,
                                              node_rect.y + 6.0f * _zoom, marker_size, marker_size};
                renderer.DrawQuad(marker_rect, MakeColorBrush(Color(0.92f, 0.22f, 0.18f, 1.0f)),
                                  Vector4f(marker_size * 0.5f), 0.22f);
                if (_zoom >= kNodeTextZoomThreshold)
                    renderer.DrawText("!", marker_rect.xy + Vector2f(marker_size * 0.34f, -1.0f * _zoom),
                                      14.0f * _zoom, Colors::kWhite);
            }

            if (_zoom >= kNodeTextZoomThreshold)
            {
                renderer.DrawText(node._display_name.empty() ? node._node_type : node._display_name,
                                  node_rect.xy + Vector2f(10.0f * _zoom, 6.0f * _zoom), 13.0f * _zoom,
                                  _style._node_title_text_color, Vector2f::kOne, nullptr);
            }

            for (const GraphPinData &pin : node._pins)
            {
                if (!pin._is_hidden)
                    DrawPin(renderer, node, pin);
            }
        }

        void GraphCanvas::DrawPin(UI::UIRenderer &renderer, const GraphNodeData &node, const GraphPinData &pin)
        {
            const Vector2f pin_screen = GetPinScreenPosition(node, pin);
            const bool is_compatible_drag_target = IsPinCompatibleDragTarget(pin._id);
            const bool is_incompatible_drag_target = IsPinIncompatibleDragTarget(pin._id);
            const f32 radius_scale = (_hovered_pin == pin._id || is_compatible_drag_target ||
                                      is_incompatible_drag_target) ? 1.25f : 1.0f;
            const f32 radius = _style._pin_radius * _zoom * radius_scale;
            const Color pin_color = is_compatible_drag_target ? _style._compatible_pin_color :
                                    is_incompatible_drag_target ? _style._incompatible_pin_color :
                                                                  GetPinColor(pin);
            renderer.DrawQuad({pin_screen.x - radius, pin_screen.y - radius, radius * 2.0f, radius * 2.0f},
                              MakeColorBrush(pin_color), Vector4f(radius), 0.2f);

            if (_zoom < kFullDetailZoomThreshold)
                return;

            const Vector2f node_size = GetNodeDisplaySize(node);
            const Vector4f node_rect = GraphRectToScreen({node._position.x, node._position.y, node_size.x,
                                                          node_size.y});
            const f32 text_y = pin_screen.y - 7.0f * _zoom;
            if (pin._direction == EGraphPinDirection::kInput)
            {
                renderer.DrawText(pin._name, {pin_screen.x + 10.0f * _zoom, text_y}, 12.0f * _zoom,
                                  _style._pin_text_color);
            }
            else
            {
                const Vector2f text_size = renderer.CalculateTextSize(pin._name, static_cast<u16>(12.0f * _zoom));
                renderer.DrawText(pin._name, {node_rect.x + node_rect.z - text_size.x - 10.0f * _zoom, text_y},
                                  12.0f * _zoom, _style._pin_text_color);
            }
        }

        void GraphCanvas::DrawMiniMap(UI::UIRenderer &renderer, const Vector4f &content_rect)
        {
            Vector4f mini_rect = Vector4f::kZero;
            Vector2f mini_offset = Vector2f::kZero;
            f32 mini_scale = 1.0f;
            if (!TryGetMiniMapGeometry(mini_rect, mini_offset, mini_scale))
                return;

            renderer.DrawQuad(mini_rect, MakeColorBrush(Color(0.02f, 0.025f, 0.03f, 0.76f)), Vector4f(4.0f), 0.35f);
            for (const GraphNodeData &node : _document->Nodes())
            {
                const Vector4f node_rect = GetNodeGraphRect(node);
                const Vector4f mini_node = {mini_offset.x + node_rect.x * mini_scale,
                                            mini_offset.y + node_rect.y * mini_scale,
                                            std::max(node_rect.z * mini_scale, 2.0f),
                                            std::max(node_rect.w * mini_scale, 2.0f)};
                const Color color = IsNodeSelected(node._id) ? _style._selected_link_color : GetNodeTitleColor(node);
                renderer.DrawQuad(mini_node, MakeColorBrush(color), Vector4f(1.0f), 0.38f);
            }
            const Vector2f graph_view_min = ScreenToGraph(content_rect.xy);
            const Vector2f graph_view_max = ScreenToGraph({content_rect.x + content_rect.z, content_rect.y + content_rect.w});
            const Vector4f mini_view = {mini_offset.x + graph_view_min.x * mini_scale,
                                        mini_offset.y + graph_view_min.y * mini_scale,
                                        (graph_view_max.x - graph_view_min.x) * mini_scale,
                                        (graph_view_max.y - graph_view_min.y) * mini_scale};
            const f32 mini_right = mini_rect.x + mini_rect.z;
            const f32 mini_bottom = mini_rect.y + mini_rect.w;
            const f32 visible_left = std::clamp(mini_view.x, mini_rect.x, mini_right);
            const f32 visible_top = std::clamp(mini_view.y, mini_rect.y, mini_bottom);
            const f32 visible_right = std::clamp(mini_view.x + mini_view.z, mini_rect.x, mini_right);
            const f32 visible_bottom = std::clamp(mini_view.y + mini_view.w, mini_rect.y, mini_bottom);
            if (visible_right > visible_left && visible_bottom > visible_top)
            {
                const Vector4f visible_view = {visible_left, visible_top, visible_right - visible_left,
                                               visible_bottom - visible_top};
                renderer.DrawQuad(visible_view, MakeColorBrush(Color(0.78f, 0.86f, 0.95f, 0.12f)), Vector4f(1.0f), 0.39f);
                renderer.DrawBox(visible_view.xy, visible_view.zw, 1.0f, Color(0.78f, 0.86f, 0.95f, 0.9f), 0.4f);
            }
        }

        void GraphCanvas::DrawAlignmentGuides(UI::UIRenderer &renderer, const Vector4f &content_rect)
        {
            if (_document == nullptr || _interaction != EInteraction::kDraggingNodes || _selected_nodes.empty())
                return;

            constexpr f32 kGuideTolerance = 6.0f;
            Vector<f32> selected_x;
            Vector<f32> selected_y;
            for (const Guid &node_id : _selected_nodes)
            {
                const GraphNodeData *node = _document->FindNode(node_id);
                if (node == nullptr)
                    continue;
                const Vector4f rect = GetNodeGraphRect(*node);
                selected_x.push_back(rect.x);
                selected_x.push_back(rect.x + rect.z * 0.5f);
                selected_x.push_back(rect.x + rect.z);
                selected_y.push_back(rect.y);
                selected_y.push_back(rect.y + rect.w * 0.5f);
                selected_y.push_back(rect.y + rect.w);
            }

            for (const GraphNodeData &node : _document->Nodes())
            {
                if (IsNodeSelected(node._id))
                    continue;
                const Vector4f rect = GetNodeGraphRect(node);
                const f32 target_x[] = {rect.x, rect.x + rect.z * 0.5f, rect.x + rect.z};
                const f32 target_y[] = {rect.y, rect.y + rect.w * 0.5f, rect.y + rect.w};
                for (const f32 x : target_x)
                {
                    if (std::any_of(selected_x.begin(), selected_x.end(), [x](f32 value)
                                    { return std::abs(value - x) <= kGuideTolerance; }))
                    {
                        const f32 screen_x = GraphToScreen({x, 0.0f}).x;
                        renderer.DrawLine({screen_x, content_rect.y}, {screen_x, content_rect.y + content_rect.w},
                                          1.0f, _style._alignment_guide_color, 0.36f);
                        break;
                    }
                }
                for (const f32 y : target_y)
                {
                    if (std::any_of(selected_y.begin(), selected_y.end(), [y](f32 value)
                                    { return std::abs(value - y) <= kGuideTolerance; }))
                    {
                        const f32 screen_y = GraphToScreen({0.0f, y}).y;
                        renderer.DrawLine({content_rect.x, screen_y}, {content_rect.x + content_rect.z, screen_y},
                                          1.0f, _style._alignment_guide_color, 0.36f);
                        break;
                    }
                }
            }
        }

        void GraphCanvas::DrawMarquee(UI::UIRenderer &renderer)
        {
            if (_interaction != EInteraction::kMarquee || _marquee_rect.z <= 1.0f || _marquee_rect.w <= 1.0f)
                return;

            renderer.DrawQuad(_marquee_rect, MakeColorBrush(_style._marquee_fill_color), Vector4f(0.0f), 0.3f);
            const Vector2f top_left = _marquee_rect.xy;
            const Vector2f top_right = {_marquee_rect.x + _marquee_rect.z, _marquee_rect.y};
            const Vector2f bottom_left = {_marquee_rect.x, _marquee_rect.y + _marquee_rect.w};
            const Vector2f bottom_right = {_marquee_rect.x + _marquee_rect.z, _marquee_rect.y + _marquee_rect.w};
            renderer.DrawLine(top_left, top_right, 1.0f, _style._marquee_border_color, 0.32f);
            renderer.DrawLine(top_right, bottom_right, 1.0f, _style._marquee_border_color, 0.32f);
            renderer.DrawLine(bottom_right, bottom_left, 1.0f, _style._marquee_border_color, 0.32f);
            renderer.DrawLine(bottom_left, top_left, 1.0f, _style._marquee_border_color, 0.32f);
        }

        void GraphCanvas::DrawConnectionMessage(UI::UIRenderer &renderer)
        {
            if (_interaction != EInteraction::kDraggingLink || _connection_message.empty())
                return;
            renderer.DrawText(_connection_message, _interaction_mouse + Vector2f(14.0f, 14.0f), 12.0f,
                              _style._message_text_color);
        }

        bool GraphCanvas::NodeHasValidationMessage(const Guid &node_id) const
        {
            if (_document == nullptr || node_id == Guid::EmptyGuid())
                return false;
            for (const GraphValidationMessage &message : _document->ValidationMessages())
            {
                if (message._node_id == node_id)
                    return true;
                if (message._pin_id != Guid::EmptyGuid())
                {
                    const GraphNodeData *pin_node = _document->FindNodeByPin(message._pin_id);
                    if (pin_node != nullptr && pin_node->_id == node_id)
                        return true;
                }
            }
            return false;
        }

        const GraphNodeData *GraphCanvas::HitTestNode(Vector2f screen_pos) const
        {
            if (_document == nullptr)
                return nullptr;

            const auto &nodes = _document->Nodes();
            for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
            {
                if (RectContains(GraphRectToScreen(GetNodeGraphRect(*it)), screen_pos))
                    return &(*it);
            }
            return nullptr;
        }

        const GraphCommentData *GraphCanvas::HitTestComment(Vector2f screen_pos) const
        {
            if (_document == nullptr)
                return nullptr;

            const auto &comments = _document->Comments();
            for (auto it = comments.rbegin(); it != comments.rend(); ++it)
            {
                const Vector4f rect = GraphRectToScreen({it->_position.x, it->_position.y, it->_size.x, it->_size.y});
                if (RectContains(rect, screen_pos))
                    return &(*it);
            }
            return nullptr;
        }

        const GraphPinData *GraphCanvas::HitTestPin(Vector2f screen_pos) const
        {
            if (_document == nullptr)
                return nullptr;

            const f32 hit_radius = std::max(_style._pin_radius * _zoom + 4.0f, 7.0f);
            const f32 hit_radius_sq = hit_radius * hit_radius;
            const auto &nodes = _document->Nodes();
            for (auto node_it = nodes.rbegin(); node_it != nodes.rend(); ++node_it)
            {
                for (const GraphPinData &pin : node_it->_pins)
                {
                    if (pin._is_hidden)
                        continue;
                    const Vector2f delta = GetPinScreenPosition(*node_it, pin) - screen_pos;
                    if (delta.x * delta.x + delta.y * delta.y <= hit_radius_sq)
                        return &pin;
                }
            }
            return nullptr;
        }

        const GraphLinkData *GraphCanvas::HitTestLink(Vector2f screen_pos) const
        {
            if (_document == nullptr)
                return nullptr;

            constexpr u32 kHitSegments = 24u;
            const f32 hit_radius = std::max(10.0f, _style._selected_link_thickness + 5.0f);
            const f32 hit_radius_sq = hit_radius * hit_radius;
            const auto &links = _document->Links();
            for (auto it = links.rbegin(); it != links.rend(); ++it)
            {
                const GraphNodeData *output_node = _document->FindNodeByPin(it->_output_pin);
                const GraphNodeData *input_node = _document->FindNodeByPin(it->_input_pin);
                const GraphPinData *output_pin = _document->FindPin(it->_output_pin);
                const GraphPinData *input_pin = _document->FindPin(it->_input_pin);
                if (output_node == nullptr || input_node == nullptr || output_pin == nullptr || input_pin == nullptr)
                    continue;

                const Vector2f start = GetPinScreenPosition(*output_node, *output_pin);
                const Vector2f end = GetPinScreenPosition(*input_node, *input_pin);
                Vector2f start_tangent = Vector2f::kZero;
                Vector2f end_tangent = Vector2f::kZero;
                GetLinkTangents(start, end, start_tangent, end_tangent);
                Vector2f previous = start;
                for (u32 index = 1u; index <= kHitSegments; ++index)
                {
                    const f32 t = static_cast<f32>(index) / static_cast<f32>(kHitSegments);
                    const Vector2f current = SampleBezier(start, start_tangent, end_tangent, end, t);
                    if (DistanceToSegmentSquared(screen_pos, previous, current) <= hit_radius_sq)
                        return &(*it);
                    previous = current;
                }
            }
            return nullptr;
        }

        bool GraphCanvas::IsNodeSelected(const Guid &node_id) const
        {
            return std::find(_selected_nodes.begin(), _selected_nodes.end(), node_id) != _selected_nodes.end();
        }

        bool GraphCanvas::IsLinkSelected(const Guid &link_id) const
        {
            return std::find(_selected_links.begin(), _selected_links.end(), link_id) != _selected_links.end();
        }

        bool GraphCanvas::IsCommentSelected(const Guid &comment_id) const
        {
            return std::find(_selected_comments.begin(), _selected_comments.end(), comment_id) != _selected_comments.end();
        }

        void GraphCanvas::SelectNode(const Guid &node_id, bool append)
        {
            if (!append)
            {
                _selected_nodes.clear();
                _selected_links.clear();
                _selected_comments.clear();
            }
            if (!IsNodeSelected(node_id))
                _selected_nodes.emplace_back(node_id);
            InvalidatePaint();
        }

        void GraphCanvas::SelectLink(const Guid &link_id, bool append)
        {
            if (!append)
            {
                _selected_nodes.clear();
                _selected_links.clear();
                _selected_comments.clear();
            }
            if (!IsLinkSelected(link_id))
                _selected_links.emplace_back(link_id);
            InvalidatePaint();
        }

        void GraphCanvas::SelectComment(const Guid &comment_id, bool append)
        {
            if (!append)
            {
                _selected_nodes.clear();
                _selected_links.clear();
                _selected_comments.clear();
            }
            if (!IsCommentSelected(comment_id))
                _selected_comments.emplace_back(comment_id);
            InvalidatePaint();
        }

        void GraphCanvas::ToggleNodeSelection(const Guid &node_id)
        {
            const auto it = std::find(_selected_nodes.begin(), _selected_nodes.end(), node_id);
            if (it != _selected_nodes.end())
                _selected_nodes.erase(it);
            else
                _selected_nodes.emplace_back(node_id);
            _selected_links.clear();
            _selected_comments.clear();
            InvalidatePaint();
        }

        void GraphCanvas::ToggleLinkSelection(const Guid &link_id)
        {
            const auto it = std::find(_selected_links.begin(), _selected_links.end(), link_id);
            if (it != _selected_links.end())
                _selected_links.erase(it);
            else
                _selected_links.emplace_back(link_id);
            _selected_nodes.clear();
            _selected_comments.clear();
            InvalidatePaint();
        }

        void GraphCanvas::ToggleCommentSelection(const Guid &comment_id)
        {
            const auto it = std::find(_selected_comments.begin(), _selected_comments.end(), comment_id);
            if (it != _selected_comments.end())
                _selected_comments.erase(it);
            else
                _selected_comments.emplace_back(comment_id);
            _selected_links.clear();
            InvalidatePaint();
        }

        void GraphCanvas::BeginNodeDrag(Vector2f mouse_pos)
        {
            _interaction = EInteraction::kDraggingNodes;
            _drag_start_mouse = mouse_pos;
            _drag_start_node_positions.clear();
            _drag_start_comment_positions.clear();
            if (_document == nullptr)
                return;

            for (const Guid &node_id : _selected_nodes)
            {
                const GraphNodeData *node = _document->FindNode(node_id);
                if (node != nullptr)
                    _drag_start_node_positions.push_back({node_id, node->_position});
            }
        }

        void GraphCanvas::BeginCommentDrag(const Guid &comment_id, Vector2f mouse_pos)
        {
            _interaction = EInteraction::kDraggingNodes;
            _drag_start_mouse = mouse_pos;
            _drag_start_node_positions.clear();
            _drag_start_comment_positions.clear();
            if (_document == nullptr)
                return;

            for (const Guid &selected_comment_id : _selected_comments)
            {
                const GraphCommentData *comment = _document->FindComment(selected_comment_id);
                if (comment != nullptr)
                    _drag_start_comment_positions.push_back({selected_comment_id, comment->_position});
            }
            const GraphCommentData *active_comment = _document->FindComment(comment_id);
            if (active_comment == nullptr)
                return;

            const Vector4f comment_rect = {active_comment->_position.x, active_comment->_position.y,
                                           active_comment->_size.x, active_comment->_size.y};
            for (const GraphNodeData &node : _document->Nodes())
            {
                const Vector4f node_rect = GetNodeGraphRect(node);
                const bool is_inside = node_rect.x >= comment_rect.x && node_rect.y >= comment_rect.y &&
                                       node_rect.x + node_rect.z <= comment_rect.x + comment_rect.z &&
                                       node_rect.y + node_rect.w <= comment_rect.y + comment_rect.w;
                if (is_inside && std::none_of(_drag_start_node_positions.begin(), _drag_start_node_positions.end(),
                                              [&node](const GraphNodePosition &position)
                                              { return position._node_id == node._id; }))
                    _drag_start_node_positions.push_back({node._id, node._position});
            }
        }

        void GraphCanvas::UpdateNodeDrag(Vector2f mouse_pos)
        {
            if (_document == nullptr || (_drag_start_node_positions.empty() && _drag_start_comment_positions.empty()))
                return;

            const Vector2f delta_graph = (mouse_pos - _drag_start_mouse) / _zoom;
            Vector<GraphNodePosition> positions;
            positions.reserve(_drag_start_node_positions.size());
            for (const GraphNodePosition &start_position : _drag_start_node_positions)
            {
                Vector2f target_position = start_position._position + delta_graph;
                if (_snap_to_grid)
                    target_position = SnapGraphPosition(target_position);
                positions.push_back({start_position._node_id, target_position});
            }
            Vector<GraphCommentPosition> comment_positions;
            comment_positions.reserve(_drag_start_comment_positions.size());
            for (const GraphCommentPosition &start_position : _drag_start_comment_positions)
            {
                Vector2f target_position = start_position._position + delta_graph;
                if (_snap_to_grid)
                    target_position = SnapGraphPosition(target_position);
                comment_positions.push_back({start_position._comment_id, target_position});
            }

            bool changed = false;
            changed |= _document->SetNodePositions(std::span<const GraphNodePosition>(positions.data(), positions.size()));
            changed |= _document->SetCommentPositions(std::span<const GraphCommentPosition>(comment_positions.data(),
                                                                                           comment_positions.size()));
            if (changed)
                InvalidatePaint();
        }

        void GraphCanvas::BeginLinkDrag(const Guid &pin_id, Vector2f mouse_pos)
        {
            _interaction = EInteraction::kDraggingLink;
            _drag_start_pin = pin_id;
            _drag_target_pin = Guid::EmptyGuid();
            _connection_message.clear();
            _drag_start_mouse = mouse_pos;
            _interaction_mouse = mouse_pos;
            InvalidatePaint();
        }

        void GraphCanvas::UpdateLinkDrag(Vector2f mouse_pos)
        {
            _interaction_mouse = mouse_pos;
            _drag_target_pin = Guid::EmptyGuid();
            _connection_message.clear();

            const GraphPinData *target_pin = HitTestPin(mouse_pos);
            if (target_pin != nullptr && !(target_pin->_id == _drag_start_pin))
            {
                _drag_target_pin = target_pin->_id;
                const GraphConnectionResponse response = GetConnectionResponse(_drag_target_pin);
                if (response._action == EGraphConnectionAction::kDisallow)
                    _connection_message = response._message;
            }
            InvalidatePaint();
        }

        void GraphCanvas::FinishLinkDrag(Vector2f mouse_pos)
        {
            const Guid source_pin = _drag_start_pin;
            UpdateLinkDrag(mouse_pos);
            if (_document != nullptr && _drag_target_pin != Guid::EmptyGuid())
            {
                const GraphConnectionResponse response = GetConnectionResponse(_drag_target_pin);
                if (response._action != EGraphConnectionAction::kDisallow)
                {
                    auto command = MakeScope<AddGraphLinkCommand>(_drag_start_pin, _drag_target_pin);
                    AddGraphLinkCommand *command_ptr = command.get();
                    const Guid link_id = _document->Commands().Execute(std::move(command)) ? command_ptr->LinkId() :
                                                                                             Guid::EmptyGuid();
                    if (link_id != Guid::EmptyGuid())
                        SelectLink(link_id, false);
                    EndInteraction();
                    return;
                }
            }
            if (_document != nullptr && source_pin != Guid::EmptyGuid())
            {
                EndInteraction();
                OpenActionMenu(mouse_pos, source_pin);
                return;
            }
            EndInteraction();
        }

        void GraphCanvas::InsertRerouteOnLink(const Guid &link_id, Vector2f screen_pos)
        {
            if (_document == nullptr || _document->FindLink(link_id) == nullptr)
                return;
            Vector2f graph_position = ScreenToGraph(screen_pos) - Vector2f(9.0f, 9.0f);
            if (_snap_to_grid)
                graph_position = SnapGraphPosition(graph_position);
            auto command = MakeScope<AddGraphRerouteNodeCommand>(link_id, graph_position);
            AddGraphRerouteNodeCommand *command_ptr = command.get();
            if (!_document->Commands().Execute(std::move(command)))
                return;
            if (command_ptr->NodeId() != Guid::EmptyGuid())
                SelectNode(command_ptr->NodeId(), false);
            _hovered_link = Guid::EmptyGuid();
            InvalidatePaint();
        }

        void GraphCanvas::BeginMiniMapDrag(Vector2f mouse_pos)
        {
            _interaction = EInteraction::kDraggingMiniMap;
            _drag_start_mouse = mouse_pos;
            UpdateMiniMapDrag(mouse_pos);
        }

        void GraphCanvas::UpdateMiniMapDrag(Vector2f mouse_pos)
        {
            Vector4f mini_rect = Vector4f::kZero;
            Vector2f mini_offset = Vector2f::kZero;
            f32 mini_scale = 1.0f;
            if (!TryGetMiniMapGeometry(mini_rect, mini_offset, mini_scale) || mini_scale <= 0.0f)
                return;
            const Vector2f graph_position = (mouse_pos - mini_offset) / mini_scale;
            CenterViewAtGraphPosition(graph_position);
        }

        void GraphCanvas::OpenActionMenu(Vector2f screen_pos, Guid source_pin)
        {
            if (_document == nullptr)
                return;

            const GraphPinData *pin = source_pin == Guid::EmptyGuid() ? nullptr : _document->FindPin(source_pin);
            GraphActionMenuContext context;
            context._document = _document;
            context._source_pin = pin;
            if (pin != nullptr)
            {
                _menu_source_pin = source_pin;
                _menu_anchor_mouse = screen_pos;
                InvalidatePaint();
            }
            else
            {
                ClearActionMenuPreview();
            }

            const bool menu_shown = GraphActionMenu::ShowAt(screen_pos, context,
                                                            [this, screen_pos, source_pin](const GraphNodeAction &action)
                                                            {
                                                                CreateNodeFromAction(action, screen_pos, source_pin);
                                                            },
                                                            [this, source_pin]()
                                                            {
                                                                ClearActionMenuPreview(source_pin);
                                                            });
            if (!menu_shown)
                ClearActionMenuPreview(source_pin);
        }

        void GraphCanvas::CreateNodeFromAction(const GraphNodeAction &action, Vector2f screen_pos, Guid source_pin)
        {
            if (_document == nullptr)
                return;

            const Vector2f graph_position = SnapGraphPosition(ScreenToGraph(screen_pos));
            auto command = MakeScope<AddGraphNodeWithConnectionCommand>(action._node_type, graph_position, source_pin);
            AddGraphNodeWithConnectionCommand *command_ptr = command.get();
            bool created = _document->Commands().Execute(std::move(command));
            Guid node_id = created ? command_ptr->NodeId() : Guid::EmptyGuid();
            if (!created && source_pin != Guid::EmptyGuid())
            {
                auto fallback_command = MakeScope<AddGraphNodeCommand>(action._node_type, graph_position);
                AddGraphNodeCommand *fallback_command_ptr = fallback_command.get();
                created = _document->Commands().Execute(std::move(fallback_command));
                node_id = created ? fallback_command_ptr->NodeId() : Guid::EmptyGuid();
            }
            if (!created)
            {
                ClearActionMenuPreview(source_pin);
                return;
            }

            if (node_id != Guid::EmptyGuid())
                SelectNode(node_id, false);
            ClearActionMenuPreview(source_pin);
            _hovered_node = Guid::EmptyGuid();
            _hovered_pin = Guid::EmptyGuid();
            _hovered_link = Guid::EmptyGuid();
            InvalidatePaint();
        }

        void GraphCanvas::ClearActionMenuPreview(Guid source_pin)
        {
            if (source_pin != Guid::EmptyGuid() && !(_menu_source_pin == source_pin))
                return;
            if (_menu_source_pin == Guid::EmptyGuid())
                return;

            _menu_source_pin = Guid::EmptyGuid();
            _menu_anchor_mouse = Vector2f::kZero;
            InvalidatePaint();
        }

        void GraphCanvas::EndInteraction()
        {
            if (_interaction == EInteraction::kNone)
                return;

            if (_interaction == EInteraction::kDraggingNodes)
                CommitNodeDrag();

            _interaction = EInteraction::kNone;
            _drag_start_node_positions.clear();
            _drag_start_comment_positions.clear();
            _drag_start_pin = Guid::EmptyGuid();
            _drag_target_pin = Guid::EmptyGuid();
            _connection_message.clear();
            _marquee_base_selection.clear();
            _is_marquee_additive = false;
            _right_pan_moved = false;
            InvalidatePaint();
        }

        void GraphCanvas::CancelInteraction()
        {
            if (_interaction == EInteraction::kDraggingNodes && _document != nullptr &&
                !_drag_start_node_positions.empty())
                _document->SetNodePositions(std::span<const GraphNodePosition>(_drag_start_node_positions.data(),
                                                                               _drag_start_node_positions.size()));
            if (_interaction == EInteraction::kDraggingNodes && _document != nullptr &&
                !_drag_start_comment_positions.empty())
                _document->SetCommentPositions(std::span<const GraphCommentPosition>(_drag_start_comment_positions.data(),
                                                                                     _drag_start_comment_positions.size()));
            _interaction = EInteraction::kNone;
            _drag_start_node_positions.clear();
            _drag_start_comment_positions.clear();
            _drag_start_pin = Guid::EmptyGuid();
            _drag_target_pin = Guid::EmptyGuid();
            _menu_source_pin = Guid::EmptyGuid();
            _menu_anchor_mouse = Vector2f::kZero;
            _connection_message.clear();
            _marquee_base_selection.clear();
            _is_marquee_additive = false;
            _right_pan_moved = false;
            InvalidatePaint();
        }

        void GraphCanvas::BeginMarquee(Vector2f mouse_pos, bool append)
        {
            _interaction = EInteraction::kMarquee;
            _drag_start_mouse = mouse_pos;
            _marquee_rect = {mouse_pos.x, mouse_pos.y, 0.0f, 0.0f};
            _is_marquee_additive = append;
            _marquee_base_selection = append ? _selected_nodes : Vector<Guid>();
            InvalidatePaint();
        }

        void GraphCanvas::UpdateMarquee(Vector2f mouse_pos)
        {
            if (_document == nullptr)
                return;

            _marquee_rect = MakePositiveRect(_drag_start_mouse, mouse_pos);
            Vector<Guid> next_selection = _is_marquee_additive ? _marquee_base_selection : Vector<Guid>();
            const Vector4f marquee_graph_rect = GetMarqueeGraphRect();
            for (const GraphNodeData &node : _document->Nodes())
            {
                if (!RectIntersects(GetNodeGraphRect(node), marquee_graph_rect))
                    continue;
                if (std::find(next_selection.begin(), next_selection.end(), node._id) == next_selection.end())
                    next_selection.emplace_back(node._id);
            }
            _selected_nodes = std::move(next_selection);
            InvalidatePaint();
        }

        void GraphCanvas::DeleteSelection()
        {
            if (_document == nullptr || (_selected_nodes.empty() && _selected_links.empty() && _selected_comments.empty()))
                return;

            Vector<Guid> remove_links = _selected_links;
            const bool removed_links = !remove_links.empty() &&
                                       _document->Commands().Execute(MakeScope<RemoveGraphLinksCommand>(remove_links));
            Vector<Guid> remove_nodes = _selected_nodes;
            const bool removed_nodes = !remove_nodes.empty() &&
                                       _document->Commands().Execute(MakeScope<RemoveGraphNodesCommand>(remove_nodes));
            Vector<Guid> remove_comments = _selected_comments;
            const bool removed_comments = !remove_comments.empty() &&
                                          _document->Commands().Execute(
                                                  MakeScope<RemoveGraphCommentsCommand>(remove_comments));
            if (removed_nodes || removed_links || removed_comments)
            {
                ClearSelection();
                _hovered_node = Guid::EmptyGuid();
                _hovered_pin = Guid::EmptyGuid();
                _hovered_link = Guid::EmptyGuid();
                _hovered_comment = Guid::EmptyGuid();
                InvalidatePaint();
            }
        }

        void GraphCanvas::CopySelection()
        {
            s_graph_clipboard = GraphClipboardPayload{};
            if (_document == nullptr || _selected_nodes.empty())
                return;

            std::set<String> selected_node_ids;
            for (const Guid &node_id : _selected_nodes)
                selected_node_ids.emplace(node_id.ToString());

            for (const Guid &node_id : _selected_nodes)
            {
                const GraphNodeData *node = _document->FindNode(node_id);
                if (node != nullptr)
                    s_graph_clipboard._nodes.emplace_back(*node);
            }

            for (const GraphLinkData &link : _document->Links())
            {
                const GraphNodeData *output_node = _document->FindNodeByPin(link._output_pin);
                const GraphNodeData *input_node = _document->FindNodeByPin(link._input_pin);
                if (output_node == nullptr || input_node == nullptr)
                    continue;
                if (selected_node_ids.contains(output_node->_id.ToString()) &&
                    selected_node_ids.contains(input_node->_id.ToString()))
                {
                    s_graph_clipboard._links.emplace_back(link);
                }
            }

            s_graph_clipboard._has_data = !s_graph_clipboard._nodes.empty();
        }

        void GraphCanvas::PasteClipboard(bool duplicate)
        {
            if (_document == nullptr || !s_graph_clipboard._has_data || s_graph_clipboard._nodes.empty())
                return;

            Vector<GraphNodeData> pasted_nodes = s_graph_clipboard._nodes;
            Vector<GraphLinkData> pasted_links = s_graph_clipboard._links;
            std::unordered_map<String, Guid> pin_map;

            bool has_bounds = false;
            Vector4f bounds = Vector4f::kZero;
            for (const GraphNodeData &node : pasted_nodes)
            {
                const Vector4f node_rect = GetNodeGraphRect(node);
                if (!has_bounds)
                {
                    bounds = node_rect;
                    has_bounds = true;
                }
                else
                {
                    const f32 min_x = std::min(bounds.x, node_rect.x);
                    const f32 min_y = std::min(bounds.y, node_rect.y);
                    const f32 max_x = std::max(bounds.x + bounds.z, node_rect.x + node_rect.z);
                    const f32 max_y = std::max(bounds.y + bounds.w, node_rect.y + node_rect.w);
                    bounds = {min_x, min_y, max_x - min_x, max_y - min_y};
                }
            }

            const Vector2f source_center = {bounds.x + bounds.z * 0.5f, bounds.y + bounds.w * 0.5f};
            const Vector2f target_center = duplicate ? source_center + Vector2f(32.0f, 32.0f) :
                                                       ScreenToGraph(_interaction_mouse);
            const Vector2f paste_offset = target_center - source_center;
            for (GraphNodeData &node : pasted_nodes)
            {
                node._id = Guid::Generate();
                node._position = SnapGraphPosition(node._position + paste_offset);
                for (GraphPinData &pin : node._pins)
                {
                    const Guid old_pin_id = pin._id;
                    pin._id = Guid::Generate();
                    pin_map[old_pin_id.ToString()] = pin._id;
                }
            }

            Vector<GraphLinkData> valid_links;
            valid_links.reserve(pasted_links.size());
            for (GraphLinkData &link : pasted_links)
            {
                const auto output_it = pin_map.find(link._output_pin.ToString());
                const auto input_it = pin_map.find(link._input_pin.ToString());
                if (output_it == pin_map.end() || input_it == pin_map.end())
                    continue;
                link._id = Guid::Generate();
                link._output_pin = output_it->second;
                link._input_pin = input_it->second;
                valid_links.emplace_back(link);
            }

            auto command = MakeScope<PasteGraphElementsCommand>(pasted_nodes, valid_links);
            PasteGraphElementsCommand *command_ptr = command.get();
            if (!_document->Commands().Execute(std::move(command)))
                return;

            _selected_nodes = command_ptr->PastedNodeIds();
            _selected_links = command_ptr->PastedLinkIds();
            _hovered_node = Guid::EmptyGuid();
            _hovered_pin = Guid::EmptyGuid();
            _hovered_link = Guid::EmptyGuid();
            InvalidatePaint();
        }

        void GraphCanvas::DuplicateSelection()
        {
            CopySelection();
            PasteClipboard(true);
        }

        void GraphCanvas::CreateCommentAroundSelection()
        {
            if (_document == nullptr)
                return;

            bool has_bounds = false;
            Vector4f bounds = Vector4f::kZero;
            if (!_selected_nodes.empty())
            {
                for (const Guid &node_id : _selected_nodes)
                {
                    const GraphNodeData *node = _document->FindNode(node_id);
                    if (node == nullptr)
                        continue;
                    const Vector4f node_rect = GetNodeGraphRect(*node);
                    if (!has_bounds)
                    {
                        bounds = node_rect;
                        has_bounds = true;
                    }
                    else
                    {
                        const f32 min_x = std::min(bounds.x, node_rect.x);
                        const f32 min_y = std::min(bounds.y, node_rect.y);
                        const f32 max_x = std::max(bounds.x + bounds.z, node_rect.x + node_rect.z);
                        const f32 max_y = std::max(bounds.y + bounds.w, node_rect.y + node_rect.w);
                        bounds = {min_x, min_y, max_x - min_x, max_y - min_y};
                    }
                }
            }
            else
            {
                const Vector4f content_rect = GetContentRect();
                const Vector2f center = ScreenToGraph({content_rect.x + content_rect.z * 0.5f,
                                                       content_rect.y + content_rect.w * 0.5f});
                bounds = {center.x - 200.0f, center.y - 120.0f, 400.0f, 240.0f};
                has_bounds = true;
            }

            if (!has_bounds)
                return;
            GraphCommentData comment;
            comment._title = "Comment";
            comment._position = {bounds.x - 28.0f, bounds.y - 42.0f};
            comment._size = {bounds.z + 56.0f, bounds.w + 72.0f};
            _document->Commands().Execute(MakeScope<AddGraphCommentCommand>(comment));
            InvalidatePaint();
        }

        void GraphCanvas::DisconnectPin(const Guid &pin_id)
        {
            if (_document == nullptr)
                return;

            Vector<const GraphLinkData *> pin_links = _document->FindLinksForPin(pin_id);
            Vector<Guid> link_ids;
            link_ids.reserve(pin_links.size());
            for (const GraphLinkData *link : pin_links)
                link_ids.emplace_back(link->_id);
            if (_document->Commands().Execute(MakeScope<RemoveGraphLinksCommand>(link_ids)))
            {
                _selected_links.clear();
                _hovered_link = Guid::EmptyGuid();
                InvalidatePaint();
            }
        }

        Vector<GraphNodePosition> GraphCanvas::GetCurrentNodePositions(
                std::span<const GraphNodePosition> source) const
        {
            Vector<GraphNodePosition> positions;
            if (_document == nullptr)
                return positions;
            positions.reserve(source.size());
            for (const GraphNodePosition &start_position : source)
            {
                const GraphNodeData *node = _document->FindNode(start_position._node_id);
                if (node != nullptr)
                    positions.push_back({start_position._node_id, node->_position});
            }
            return positions;
        }

        Vector<GraphCommentPosition> GraphCanvas::GetCurrentCommentPositions(
                std::span<const GraphCommentPosition> source) const
        {
            Vector<GraphCommentPosition> positions;
            if (_document == nullptr)
                return positions;
            positions.reserve(source.size());
            for (const GraphCommentPosition &start_position : source)
            {
                const GraphCommentData *comment = _document->FindComment(start_position._comment_id);
                if (comment != nullptr)
                    positions.push_back({start_position._comment_id, comment->_position});
            }
            return positions;
        }

        void GraphCanvas::CommitNodeDrag()
        {
            if (_document == nullptr || (_drag_start_node_positions.empty() && _drag_start_comment_positions.empty()))
                return;

            Vector<GraphNodePosition> target_positions = GetCurrentNodePositions(
                    std::span<const GraphNodePosition>(_drag_start_node_positions.data(),
                                                       _drag_start_node_positions.size()));
            Vector<GraphCommentPosition> target_comment_positions = GetCurrentCommentPositions(
                    std::span<const GraphCommentPosition>(_drag_start_comment_positions.data(),
                                                          _drag_start_comment_positions.size()));
            _document->SetNodePositions(std::span<const GraphNodePosition>(_drag_start_node_positions.data(),
                                                                           _drag_start_node_positions.size()));
            _document->SetCommentPositions(std::span<const GraphCommentPosition>(_drag_start_comment_positions.data(),
                                                                                 _drag_start_comment_positions.size()));
            if (!target_positions.empty() || !target_comment_positions.empty())
            {
                _document->Commands().Execute(MakeScope<MoveGraphSelectionCommand>(_drag_start_node_positions,
                                                                                   target_positions,
                                                                                   _drag_start_comment_positions,
                                                                                   target_comment_positions));
            }
        }

        void GraphCanvas::FocusSelection()
        {
            FocusNodes(std::span<const Guid>(_selected_nodes.data(), _selected_nodes.size()));
        }

        bool GraphCanvas::FocusNode(const Guid &node_id, bool select)
        {
            if (_document == nullptr || _document->FindNode(node_id) == nullptr)
                return false;
            if (select)
                SelectNode(node_id, false);
            FocusNodes(std::span<const Guid>(&node_id, 1u));
            return true;
        }

        bool GraphCanvas::FocusLink(const Guid &link_id, bool select)
        {
            if (_document == nullptr)
                return false;
            const GraphLinkData *link = _document->FindLink(link_id);
            if (link == nullptr)
                return false;
            if (select)
                SelectLink(link_id, false);
            Vector<Guid> node_ids;
            if (const GraphNodeData *node = _document->FindNodeByPin(link->_output_pin); node != nullptr)
                node_ids.emplace_back(node->_id);
            if (const GraphNodeData *node = _document->FindNodeByPin(link->_input_pin); node != nullptr)
                node_ids.emplace_back(node->_id);
            FocusNodes(std::span<const Guid>(node_ids.data(), node_ids.size()));
            return !node_ids.empty();
        }

        bool GraphCanvas::SearchAndFocusNode(StringView search_text)
        {
            if (_document == nullptr || search_text.empty())
                return false;
            const String search = ToLowerCopy(String(search_text));
            for (const GraphNodeData &node : _document->Nodes())
            {
                if (ContainsSearchText(node._display_name, search) || ContainsSearchText(node._node_type, search))
                    return FocusNode(node._id, true);
            }
            return false;
        }

        void GraphCanvas::FocusAll()
        {
            if (_document == nullptr)
                return;

            Vector<Guid> node_ids;
            node_ids.reserve(_document->Nodes().size());
            for (const GraphNodeData &node : _document->Nodes())
                node_ids.emplace_back(node._id);
            FocusNodes(std::span<const Guid>(node_ids.data(), node_ids.size()));
        }

        void GraphCanvas::FocusNodes(std::span<const Guid> node_ids)
        {
            if (_document == nullptr || node_ids.empty())
                return;

            bool has_bounds = false;
            Vector4f bounds = Vector4f::kZero;
            for (const Guid &node_id : node_ids)
            {
                const GraphNodeData *node = _document->FindNode(node_id);
                if (node == nullptr)
                    continue;

                const Vector4f node_rect = GetNodeGraphRect(*node);
                if (!has_bounds)
                {
                    bounds = node_rect;
                    has_bounds = true;
                }
                else
                {
                    const f32 min_x = std::min(bounds.x, node_rect.x);
                    const f32 min_y = std::min(bounds.y, node_rect.y);
                    const f32 max_x = std::max(bounds.x + bounds.z, node_rect.x + node_rect.z);
                    const f32 max_y = std::max(bounds.y + bounds.w, node_rect.y + node_rect.w);
                    bounds = {min_x, min_y, max_x - min_x, max_y - min_y};
                }
            }

            if (!has_bounds)
                return;

            const Vector4f content_rect = GetContentRect();
            if (content_rect.z <= 0.0f || content_rect.w <= 0.0f)
                return;

            constexpr f32 kFocusPadding = 80.0f;
            const f32 available_width = std::max(content_rect.z - kFocusPadding, 1.0f);
            const f32 available_height = std::max(content_rect.w - kFocusPadding, 1.0f);
            const f32 target_zoom = std::min(available_width / std::max(bounds.z, 1.0f),
                                             available_height / std::max(bounds.w, 1.0f));
            _zoom = std::clamp(target_zoom, _min_zoom, _max_zoom);

            const Vector2f bounds_center = {bounds.x + bounds.z * 0.5f, bounds.y + bounds.w * 0.5f};
            const Vector2f content_center = {content_rect.z * 0.5f, content_rect.w * 0.5f};
            _view_offset = content_center - bounds_center * _zoom;
            InvalidatePaint();
        }

        bool GraphCanvas::TryGetMiniMapGeometry(Vector4f &mini_rect, Vector2f &mini_offset, f32 &mini_scale) const
        {
            const Vector4f content_rect = GetContentRect();
            if (_document == nullptr || _document->Nodes().empty() || content_rect.z < 260.0f || content_rect.w < 220.0f)
                return false;

            bool has_bounds = false;
            Vector4f graph_bounds = Vector4f::kZero;
            for (const GraphNodeData &node : _document->Nodes())
            {
                const Vector4f node_rect = GetNodeGraphRect(node);
                if (!has_bounds)
                {
                    graph_bounds = node_rect;
                    has_bounds = true;
                }
                else
                {
                    const f32 min_x = std::min(graph_bounds.x, node_rect.x);
                    const f32 min_y = std::min(graph_bounds.y, node_rect.y);
                    const f32 max_x = std::max(graph_bounds.x + graph_bounds.z, node_rect.x + node_rect.z);
                    const f32 max_y = std::max(graph_bounds.y + graph_bounds.w, node_rect.y + node_rect.w);
                    graph_bounds = {min_x, min_y, max_x - min_x, max_y - min_y};
                }
            }
            if (!has_bounds || graph_bounds.z <= 0.0f || graph_bounds.w <= 0.0f)
                return false;

            graph_bounds.x -= 120.0f;
            graph_bounds.y -= 120.0f;
            graph_bounds.z += 240.0f;
            graph_bounds.w += 240.0f;
            mini_rect = {content_rect.x + content_rect.z - 172.0f, content_rect.y + 14.0f, 158.0f, 110.0f};
            mini_scale = std::min(mini_rect.z / graph_bounds.z, mini_rect.w / graph_bounds.w);
            mini_offset = mini_rect.xy - graph_bounds.xy * mini_scale +
                          Vector2f((mini_rect.z - graph_bounds.z * mini_scale) * 0.5f,
                                   (mini_rect.w - graph_bounds.w * mini_scale) * 0.5f);
            return mini_scale > 0.0f;
        }

        bool GraphCanvas::HitTestMiniMap(Vector2f screen_pos) const
        {
            Vector4f mini_rect = Vector4f::kZero;
            Vector2f mini_offset = Vector2f::kZero;
            f32 mini_scale = 1.0f;
            return TryGetMiniMapGeometry(mini_rect, mini_offset, mini_scale) && RectContains(mini_rect, screen_pos);
        }

        void GraphCanvas::CenterViewAtGraphPosition(Vector2f graph_position)
        {
            const Vector4f content_rect = GetContentRect();
            _view_offset = Vector2f(content_rect.z * 0.5f, content_rect.w * 0.5f) - graph_position * _zoom;
            InvalidatePaint();
        }

        bool GraphCanvas::AutoScrollCanvas(Vector2f mouse_pos, f32 dt)
        {
            const Vector4f content_rect = GetContentRect();
            Vector2f delta = Vector2f::kZero;
            if (mouse_pos.x < content_rect.x + kAutoScrollEdgeSize)
                delta.x = kAutoScrollSpeed * dt;
            else if (mouse_pos.x > content_rect.x + content_rect.z - kAutoScrollEdgeSize)
                delta.x = -kAutoScrollSpeed * dt;
            if (mouse_pos.y < content_rect.y + kAutoScrollEdgeSize)
                delta.y = kAutoScrollSpeed * dt;
            else if (mouse_pos.y > content_rect.y + content_rect.w - kAutoScrollEdgeSize)
                delta.y = -kAutoScrollSpeed * dt;
            if (delta.x == 0.0f && delta.y == 0.0f)
                return false;
            _view_offset += delta;
            _drag_start_mouse += delta;
            InvalidatePaint();
            return true;
        }

        Vector4f GraphCanvas::GetNodeGraphRect(const GraphNodeData &node) const
        {
            const Vector2f node_size = GetNodeDisplaySize(node);
            return {node._position.x, node._position.y, node_size.x, node_size.y};
        }

        Vector4f GraphCanvas::GetMarqueeGraphRect() const
        {
            const Vector2f graph_min = ScreenToGraph(_marquee_rect.xy);
            const Vector2f graph_max = ScreenToGraph({_marquee_rect.x + _marquee_rect.z,
                                                      _marquee_rect.y + _marquee_rect.w});
            return MakePositiveRect(graph_min, graph_max);
        }

        Vector2f GraphCanvas::SnapGraphPosition(Vector2f position) const
        {
            if (_grid_snap <= 0.0f)
                return position;
            return {std::round(position.x / _grid_snap) * _grid_snap,
                    std::round(position.y / _grid_snap) * _grid_snap};
        }

        GraphConnectionResponse GraphCanvas::GetConnectionResponse(const Guid &target_pin_id) const
        {
            if (_document == nullptr || _drag_start_pin == Guid::EmptyGuid() || target_pin_id == Guid::EmptyGuid())
                return GraphConnectionResponse::Disallow("");
            return _document->CanConnect(_drag_start_pin, target_pin_id);
        }

        bool GraphCanvas::IsPinCompatibleDragTarget(const Guid &pin_id) const
        {
            if (_interaction != EInteraction::kDraggingLink || pin_id == _drag_start_pin)
                return false;
            return GetConnectionResponse(pin_id)._action != EGraphConnectionAction::kDisallow;
        }

        bool GraphCanvas::IsPinIncompatibleDragTarget(const Guid &pin_id) const
        {
            if (_interaction != EInteraction::kDraggingLink || pin_id == _drag_start_pin)
                return false;
            return GetConnectionResponse(pin_id)._action == EGraphConnectionAction::kDisallow;
        }
    } // namespace Editor
} // namespace Ailu
