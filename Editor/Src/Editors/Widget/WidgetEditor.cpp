#include "Editors/Widget/WidgetEditor.h"

#include "Assets/WidgetAsset.h"
#include "Common/EditorPopup.h"
#include "Common/Undo.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Inspector/ReflectedPropertyPanel.h"
#include "Objects/JsonArchive.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/Container.h"
#include "UI/DragDrop.h"
#include "UI/TreeView.h"
#include "UI/UIFramework.h"
#include "UI/UIRenderer.h"
#include "UI/Widget.h"
#include "UI/Style/UIStyleBasic.h"
#include "Render/Texture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            PropertyInfo *FindPropertyInHierarchy(const Type *type, const String &name)
            {
                for (auto *current_type = type; current_type != nullptr; current_type = current_type->BaseType())
                {
                    if (auto *property = current_type->FindPropertyByName(name); property != nullptr)
                        return property;
                }
                return nullptr;
            }

            void SetCanvasSlotValues(UI::CanvasSlot *slot, Vector2f position, Vector2f size, bool size_to_content, Vector2f anchor,
                                     bool notify_observers)
            {
                if (slot == nullptr) return;
                if (!notify_observers)
                {
                    slot->Position(position).Size(size).SizeToContent(size_to_content).Anchor(anchor);
                    return;
                }
                const Type *slot_type = slot->GetType();
                if (auto *property = FindPropertyInHierarchy(slot_type, "_position"); property != nullptr)
                    property->Set<Vector2f>(slot, position, PropertyInfo::EPropertyChangeSource::kUI);
                if (auto *property = FindPropertyInHierarchy(slot_type, "_size"); property != nullptr)
                    property->Set<Vector2f>(slot, size, PropertyInfo::EPropertyChangeSource::kUI);
                if (auto *property = FindPropertyInHierarchy(slot_type, "_size_to_content"); property != nullptr)
                    property->Set<bool>(slot, size_to_content, PropertyInfo::EPropertyChangeSource::kUI);
                if (auto *property = FindPropertyInHierarchy(slot_type, "_anchor"); property != nullptr)
                    property->Set<Vector2f>(slot, anchor, PropertyInfo::EPropertyChangeSource::kUI);
            }
        }

        namespace
        {
            constexpr f32 kToolbarHeight = 30.0f;
            constexpr f32 kStatusBarHeight = 22.0f;
            constexpr f32 kLeftPanelRatio = 0.23f;
            constexpr f32 kCenterPanelRatio = 0.70f;
            constexpr f32 kDesignerHandleSize = 8.0f;
            constexpr f32 kDesignerSnapSize = 8.0f;
            constexpr f32 kMinimumCanvasSize = 1.0f;

            enum class EWidgetDesignerHandle
            {
                kNone, kMove, kAnchor, kLeft, kRight, kTop, kBottom, kTopLeft, kTopRight, kBottomLeft, kBottomRight
            };

            struct CanvasEditState
            {
                Vector2f _position = Vector2f::kZero;
                Vector2f _size = Vector2f::kZero;
                Vector2f _anchor = Vector2f::kZero;
                bool _size_to_content = false;

                bool operator==(const CanvasEditState &other) const
                {
                    return NearbyEqual(_position, other._position) && NearbyEqual(_size, other._size) &&
                           NearbyEqual(_anchor, other._anchor) &&
                           _size_to_content == other._size_to_content;
                }
            };

            bool IsTypeDerivedFrom(const Type *type, const Type *base_type)
            {
                for (const Type *current_type = type; current_type != nullptr; current_type = current_type->BaseType())
                {
                    if (current_type == base_type)
                        return true;
                }
                return false;
            }

            bool IsLayoutElementType(const Type *type)
            {
                return IsTypeDerivedFrom(type, StaticClass<UI::Canvas>()) ||
                       IsTypeDerivedFrom(type, StaticClass<UI::LinearBox>()) ||
                       IsTypeDerivedFrom(type, StaticClass<UI::ScrollView>());
            }

            bool ContainsInsensitive(const String &value, const String &query)
            {
                if (query.empty()) return true;
                if (query.size() > value.size()) return false;
                for (size_t start = 0u; start + query.size() <= value.size(); ++start)
                {
                    bool matches = true;
                    for (size_t index = 0u; index < query.size(); ++index)
                    {
                        const auto lhs = static_cast<unsigned char>(value[start + index]);
                        const auto rhs = static_cast<unsigned char>(query[index]);
                        if (std::tolower(lhs) != std::tolower(rhs))
                        {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) return true;
                }
                return false;
            }

            UI::VerticalBox *AddPanel(UI::UIElement *parent, const String &title, Color background)
            {
                auto *border = parent->AddChild<UI::Border>();
                border->_bg_color = background;
                auto *panel = border->AddChild<UI::VerticalBox>();
                panel->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
                panel->SlotPadding() = UI::Padding(8.0f);
                auto *header = panel->AddChild<UI::Text>(title);
                header->FontSize(16.0f);
                return panel;
            }

            String TrimName(const String &value)
            {
                size_t first = 0u;
                size_t last = value.size();
                while (first < last && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
                while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1u]))) --last;
                return value.substr(first, last - first);
            }

            String SerializePropertyValue(const PropertyInfo &property, void *instance)
            {
                JsonArchive archive;
                property.Serialize(instance, archive);
                return archive.SaveToString();
            }

            String SerializeWidgetTree(const Ref<UI::UIElement> &root)
            {
                const String root_name = "_widget_editor_root";
                Ref<UI::UIElement> root_copy = root;
                JsonArchive archive;
                SerializerWrapper<Ref<UI::UIElement>>::Serialize(&root_copy, archive, &root_name);
                return archive.SaveToString();
            }

            void EnsureUniqueWidgetGuids(UI::UIElement *root)
            {
                std::unordered_set<Guid, GuidHasher> guids;
                const auto visit = [&guids](const auto &self, UI::UIElement *element) -> void
                {
                    if (element == nullptr)
                        return;
                    const Guid guid = element->GuidValue();
                    if (guid.IsEmpty() || !guids.insert(guid).second)
                    {
                        element->RegenerateGuid();
                        guids.insert(element->GuidValue());
                    }
                    for (const Ref<UI::UIElement> &child : element->GetChildren())
                        self(self, child.get());
                };
                visit(visit, root);
            }

            void InvalidateLayoutToRoot(UI::UIElement *element)
            {
                for (UI::UIElement *current = element; current != nullptr; current = current->GetParent())
                    current->InvalidateLayout();
            }

            bool IsEditorVisibleProperty(const PropertyInfo &property)
            {
                static const std::unordered_set<String> s_hidden_properties = {
                    "_id", "_hash", "_guid", "_parent", "_children", "_slot_obj", "_owning_widget", "_state_flags",
                    "_property_visual_override_flags",
                    "_is_style_dirty", "_paint_dirty", "_is_layout_dirty", "_is_transf_dirty", "_dirty_reasons",
                    "_debug_paint_dirty", "_debug_dirty_reasons", "_hierarchy_depth", "_depth"
                };
                return !s_hidden_properties.contains(property.Name());
            }
        }

        struct WidgetEditSession
        {
            Weak<WidgetAsset> _working_asset;
            Guid _selected_guid = Guid::EmptyGuid();
            bool _refresh_pending = false;
            Guid _canvas_element_guid = Guid::EmptyGuid();
            CanvasEditState _canvas_edit_state;
            bool _canvas_refresh_pending = false;
            Guid _property_element_guid = Guid::EmptyGuid();
            String _property_name;
            String _property_snapshot;
            bool _property_is_slot = false;
            bool _property_refresh_pending = false;
        };

        class WidgetTreeEditCommand final : public ICommand
        {
            DECLARE_COMMAND(WidgetTreeEdit)
        public:
            WidgetTreeEditCommand(Weak<WidgetEditSession> session, Ref<UI::UIElement> before_root,
                                  Ref<UI::UIElement> after_root, Guid before_selection, Guid after_selection)
                : _session(std::move(session)), _before_root(std::move(before_root)), _after_root(std::move(after_root)),
                  _before_selection(std::move(before_selection)), _after_selection(std::move(after_selection))
            {
            }

            void Execute() override
            {
                // The edit has already been applied to the working tree before it is recorded.  Replacing that tree
                // here would invalidate editor pointers (notably _selected_element) during the current input event.
                if (_is_initial_execute)
                {
                    _is_initial_execute = false;
                    return;
                }
                Apply(_after_root, _after_selection);
            }

            void Undo() override
            {
                Apply(_before_root, _before_selection);
            }

        private:
            void Apply(const Ref<UI::UIElement> &root, const Guid &selected_guid)
            {
                Ref<WidgetEditSession> session = _session.lock();
                if (session == nullptr)
                    return;
                Ref<WidgetAsset> working_asset = session->_working_asset.lock();
                if (working_asset == nullptr)
                    return;
                working_asset->SetRoot(CloneUIElementTree(root));
                session->_selected_guid = selected_guid;
                session->_refresh_pending = true;
            }

        private:
            Weak<WidgetEditSession> _session;
            Ref<UI::UIElement> _before_root;
            Ref<UI::UIElement> _after_root;
            Guid _before_selection = Guid::EmptyGuid();
            Guid _after_selection = Guid::EmptyGuid();
            bool _is_initial_execute = true;
        };

        class WidgetCanvasEditCommand final : public ICommand
        {
            DECLARE_COMMAND(WidgetCanvasEdit)
        public:
            WidgetCanvasEditCommand(Weak<WidgetEditSession> session, Guid element_guid, CanvasEditState before,
                                    CanvasEditState after)
                : _session(std::move(session)), _element_guid(std::move(element_guid)), _before(before), _after(after)
            {
            }

            void Execute() override
            {
                if (_is_initial_execute)
                {
                    _is_initial_execute = false;
                    return;
                }
                Apply(_after);
            }

            void Undo() override
            {
                Apply(_before);
            }

        private:
            void Apply(const CanvasEditState &state)
            {
                Ref<WidgetEditSession> session = _session.lock();
                if (session == nullptr)
                    return;
                Ref<WidgetAsset> working_asset = session->_working_asset.lock();
                if (working_asset == nullptr || working_asset->Root() == nullptr)
                    return;
                UI::UIElement *element = working_asset->Root()->FindChildByGuid(_element_guid);
                auto *slot = element == nullptr ? nullptr : dynamic_cast<UI::CanvasSlot *>(element->GetSlot().get());
                if (slot == nullptr)
                    return;
                SetCanvasSlotValues(slot, state._position, state._size, state._size_to_content, state._anchor, true);
                session->_canvas_element_guid = _element_guid;
                session->_canvas_edit_state = state;
                session->_canvas_refresh_pending = true;
            }

            Weak<WidgetEditSession> _session;
            Guid _element_guid = Guid::EmptyGuid();
            CanvasEditState _before;
            CanvasEditState _after;
            bool _is_initial_execute = true;
        };

        class WidgetPropertyEditCommand final : public ICommand
        {
            DECLARE_COMMAND(WidgetPropertyEdit)
        public:
            WidgetPropertyEditCommand(Weak<WidgetEditSession> session, Guid element_guid, String property_name,
                                      bool is_slot, String before, String after)
                : _session(std::move(session)), _element_guid(std::move(element_guid)), _property_name(std::move(property_name)),
                  _is_slot(is_slot), _before(std::move(before)), _after(std::move(after))
            {
            }

            void Execute() override
            {
                if (_is_initial_execute)
                {
                    _is_initial_execute = false;
                    return;
                }
                Apply(_after);
            }

            void Undo() override
            {
                Apply(_before);
            }

        private:
            void Apply(const String &snapshot)
            {
                Ref<WidgetEditSession> session = _session.lock();
                if (session == nullptr)
                    return;
                Ref<WidgetAsset> working_asset = session->_working_asset.lock();
                UI::UIElement *element = working_asset == nullptr || working_asset->Root() == nullptr ? nullptr :
                    working_asset->Root()->FindChildByGuid(_element_guid);
                void *instance = _is_slot && element != nullptr ? static_cast<void *>(element->GetSlot().get()) :
                    static_cast<void *>(element);
                const Type *type = instance == nullptr ? nullptr :
                    (_is_slot ? static_cast<UI::UISlot *>(instance)->GetType() : element->GetType());
                PropertyInfo *property = FindPropertyInHierarchy(type, _property_name);
                if (property == nullptr)
                    return;
                JsonArchive archive;
                if (!archive.LoadFromString(snapshot))
                    return;
                property->Deserialize(instance, archive);
                property->NotifyObject(static_cast<Object *>(instance), PropertyInfo::EPropertyChangeSource::kUndo);
                if (_is_slot)
                    InvalidateLayoutToRoot(element);
                else
                    element->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
                session->_property_element_guid = _element_guid;
                session->_property_name = _property_name;
                session->_property_snapshot = snapshot;
                session->_property_is_slot = _is_slot;
                session->_property_refresh_pending = true;
            }

            Weak<WidgetEditSession> _session;
            Guid _element_guid = Guid::EmptyGuid();
            String _property_name;
            bool _is_slot = false;
            String _before;
            String _after;
            bool _is_initial_execute = true;
        };

        class WidgetDesignerPreview final : public UI::UIElement
        {
        public:
            explicit WidgetDesignerPreview(WidgetEditor *editor) : _editor(editor)
            {
                SetWantsMouseEvents(true);
                SetInteractiveEnabled(true);
                UI::DropHandler drop_handler;
                drop_handler._can_drop = [](const UI::DragPayload &payload)
                {
                    return payload._type == UI::EDragType::kUIWidget && payload._data != nullptr;
                };
                drop_handler._on_drop = [this](const UI::DragPayload &payload, f32 x, f32 y)
                {
                    const auto *element_type = static_cast<const Type *>(payload._data);
                    if (_editor == nullptr || element_type == nullptr) return;
                    UI::UIElement *parent = ElementAt({x, y});
                    while (parent != nullptr && parent->As<UI::Canvas>() == nullptr &&
                           parent->As<UI::LinearBox>() == nullptr && parent->As<UI::Border>() == nullptr)
                        parent = parent->GetParent();
                    if (parent == nullptr && _editor->WorkingAsset() != nullptr)
                        parent = _editor->WorkingRoot();
                    _editor->AddPaletteElement(element_type, parent, _editor->ScreenToDesign({x, y}));
                };
                SetDropHandler(std::move(drop_handler));
                OnMouseDown() += [this](UI::UIEvent &event)
                {
                    if (_editor != nullptr && _editor->_is_preview_mode)
                    {
                        _editor->ForwardPreviewEvent(event);
                        event._is_handled = true;
                        return;
                    }
                    _last_mouse = event._mouse_position;
                    _is_panning = event._key_code == EKey::kRBUTTON;
                    if (_is_panning)
                    {
                        Application::Get().SetCursor(ECursorType::kHand, ECursorPriority::kHigh);
                    }
                    else if (event._key_code == EKey::kLBUTTON)
                    {
                        const EWidgetDesignerHandle handle = HitTestHandle(event._mouse_position);
                        if (handle != EWidgetDesignerHandle::kNone) BeginCanvasDrag(handle, event._mouse_position);
                        else
                        {
                            UI::UIElement *hit_element = ElementAt(event._mouse_position);
                            _editor->_hovered_element = hit_element;
                            if (CanEditCanvasElement(hit_element))
                            {
                                _editor->SetSelectedElement(hit_element);
                                BeginCanvasDrag(EWidgetDesignerHandle::kMove, event._mouse_position);
                            }
                            else if (CanEditCanvasElement(_editor->_selected_element) &&
                                     UI::UIElement::IsPointInside(event._mouse_position,
                                                                  GetElementScreenRect(_editor->_selected_element)))
                            {
                                BeginCanvasDrag(EWidgetDesignerHandle::kMove, event._mouse_position);
                            }
                            else if (hit_element != nullptr)
                            {
                                _editor->SetSelectedElement(hit_element);
                            }
                            InvalidatePaint();
                        }
                    }
                    event._is_handled = true;
                };
                OnMouseMove() += [this](UI::UIEvent &event)
                {
                    if (_editor == nullptr) return;
                    if (_editor->_is_preview_mode)
                    {
                        const u64 frame = Application::Get().GetFrameCount();
                        const bool is_duplicate = _preview_mouse_move_frame == frame &&
                                                  NearbyEqual(_preview_mouse_move_pos, event._mouse_position) &&
                                                  NearbyEqual(_preview_mouse_move_delta, event._mouse_delta);
                        if (!is_duplicate)
                        {
                            _editor->ForwardPreviewEvent(event);
                            _preview_mouse_move_frame = frame;
                            _preview_mouse_move_pos = event._mouse_position;
                            _preview_mouse_move_delta = event._mouse_delta;
                        }
                        event._is_handled = true;
                        return;
                    }
                    if (_is_panning)
                    {
                        Application::Get().SetCursor(ECursorType::kHand, ECursorPriority::kHigh);
                        _editor->_designer_pan += event._mouse_position - _last_mouse;
                        InvalidatePaint();
                    }
                    else if (_is_canvas_dragging)
                    {
                        UpdateHandleCursor(_canvas_drag_handle);
                        UpdateCanvasDrag(event._mouse_position);
                    }
                    else
                    {
                        UpdateHandleCursor(HitTestHandle(event._mouse_position));
                        _editor->_hovered_element = ElementAt(event._mouse_position);
                        InvalidatePaint();
                    }
                    _last_mouse = event._mouse_position;
                    event._is_handled = true;
                };
                OnMouseUp() += [this](UI::UIEvent &event)
                {
                    if (_editor != nullptr && _editor->_is_preview_mode)
                    {
                        _editor->ForwardPreviewEvent(event);
                        event._is_handled = true;
                        return;
                    }
                    if (event._key_code == EKey::kRBUTTON)
                        _is_panning = false;
                    else if (event._key_code == EKey::kLBUTTON)
                        EndCanvasDrag();
                    event._is_handled = true;
                };
                OnMouseScroll() += [this](UI::UIEvent &event)
                {
                    if (_editor == nullptr || _editor->WorkingAsset() == nullptr) return;
                    if (_editor->_is_preview_mode)
                    {
                        _editor->ForwardPreviewEvent(event);
                        event._is_handled = true;
                        return;
                    }
                    const Vector2f design_pos = _editor->ScreenToDesign(event._mouse_position);
                    const f32 old_zoom = _editor->_designer_zoom;
                    if (event._scroll_delta > 0.0f)
                        _editor->_designer_zoom = std::min(_editor->_designer_zoom * 1.1f, 8.0f);
                    else if (event._scroll_delta < 0.0f)
                        _editor->_designer_zoom = std::max(_editor->_designer_zoom / 1.1f, 0.05f);
                    if (!NearbyEqual(old_zoom, _editor->_designer_zoom))
                    {
                        const Vector4f rect = GetContentRect();
                        const Vector2f preview_size = _editor->WorkingAsset()->DesignSize() * _editor->_designer_zoom;
                        _editor->_designer_pan = event._mouse_position - rect.xy - rect.zw * 0.5f + preview_size * 0.5f -
                                                 design_pos * _editor->_designer_zoom;
                        _editor->RefreshDesigner();
                    }
                    InvalidatePaint();
                    event._is_handled = true;
                };
                OnMouseExit() += [this](UI::UIEvent &event)
                {
                    if (_editor == nullptr || !_editor->_is_preview_mode)
                        return;
                    _editor->ForwardPreviewEvent(event);
                    event._is_handled = true;
                };
                OnKeyDown() += [this](UI::UIEvent &event)
                {
                    if (_editor == nullptr || !_editor->_is_preview_mode)
                        return;
                    _editor->ForwardPreviewEvent(event);
                    event._is_handled = true;
                };
                OnKeyUp() += [this](UI::UIEvent &event)
                {
                    if (_editor == nullptr || !_editor->_is_preview_mode)
                        return;
                    _editor->ForwardPreviewEvent(event);
                    event._is_handled = true;
                };
            }

            Vector2f MeasureDesiredSize() override { return {480.0f, 320.0f}; }

        protected:
            void RenderImpl(UI::UIRenderer &renderer) override
            {
                const Vector4f content_rect = GetContentRect();
                if (_editor == nullptr || content_rect.z <= 0.0f || content_rect.w <= 0.0f) return;
                renderer.PushScissor(content_rect);
                UI::UIBrush background;
                background._type = UI::EUIBrushType::kColor;
                background._tint = Color(0.08f, 0.09f, 0.10f, 1.0f);
                renderer.DrawQuad(content_rect, background, -0.2f);
                if (_editor->WorkingAsset() != nullptr)
                {
                    if (!_editor->_is_preview_mode)
                        DrawGrid(renderer, content_rect);
                    const Vector2f size = _editor->WorkingAsset()->DesignSize() * _editor->_designer_zoom;
                    const Vector4f preview_rect(_editor->DesignToScreen(Vector2f::kZero), size);
                    if (_editor->_preview_render_texture != nullptr)
                        renderer.DrawImage(_editor->_preview_render_texture.get(), preview_rect);
                    renderer.DrawBox(preview_rect.xy, preview_rect.zw, 1.0f, Color(0.40f, 0.42f, 0.46f, 1.0f), 0.4f);
                    if (!_editor->_is_preview_mode)
                    {
                        DrawElementOutline(renderer, _editor->_hovered_element, Color(0.95f, 0.70f, 0.18f, 1.0f), 0.6f);
                        DrawElementOutline(renderer, _editor->_selected_element, Color(0.18f, 0.62f, 1.0f, 1.0f), 0.7f);
                        DrawAnchorMarker(renderer);
                        DrawAlignmentGuides(renderer, content_rect);
                        DrawResizeHandles(renderer);
                    }
                }
                renderer.PopScissor();
            }

            void Update(f32 dt) override
            {
                UIElement::Update(dt);
                if (_editor != nullptr && _editor->_designer_needs_fit && _editor->WorkingAsset() != nullptr)
                    _editor->FitDesigner();
                if (_is_canvas_dragging && Input::IsKeyDownAccurate(EKey::kESCAPE))
                    CancelCanvasDrag();
            }

        private:
            UI::UIElement *ElementAt(Vector2f screen_pos) const
            {
                if (_editor == nullptr || _editor->_preview_widget == nullptr) return nullptr;
                UI::UIElement *root = _editor->_preview_widget->Root();
                if (root == nullptr) return nullptr;
                root->InvalidateLayout(true);
                _editor->_preview_widget->Update(0.0f);
                const Vector2f design_pos = _editor->ScreenToDesign(screen_pos);
                const Vector2f preview_root_origin = root->GetArrangeRect().xy;
                UI::UIElement *hit = root->HitTest(design_pos + preview_root_origin);
                if (hit == nullptr && UI::UIElement::IsPointInside(design_pos + preview_root_origin, root->GetArrangeRect())) hit = root;
                UI::UIElement *asset_element = _editor->ResolveAssetElement(hit);

                // The preview is rebuilt from a clone. During the frame in which a layout is invalidated its
                // arrange rectangles can still be stale, while the editor-side CanvasSlot already contains the
                // authoritative design geometry. Traverse editor rectangles as well, because controls such as Button
                // intentionally return themselves from runtime HitTest and hide their content children.
                UI::UIElement *working_root = _editor->WorkingRoot();
                const auto find_element_by_rect = [this, design_pos](const auto &self,
                                                                      UI::UIElement *parent) -> UI::UIElement *
                {
                    if (parent == nullptr)
                        return nullptr;
                    const auto &children = parent->GetChildren();
                    for (auto iter = children.rbegin(); iter != children.rend(); ++iter)
                    {
                        UI::UIElement *child = iter->get();
                        if (child == nullptr)
                            continue;
                        const Vector4f child_rect = GetElementDesignRect(child);
                        if (!UI::UIElement::IsPointInside(design_pos, child_rect))
                            continue;
                        if (UI::UIElement *nested = self(self, child); nested != nullptr)
                            return nested;
                        return child;
                    }
                    return nullptr;
                };
                if (UI::UIElement *fallback = find_element_by_rect(find_element_by_rect, working_root);
                    fallback != nullptr)
                    return fallback;
                return asset_element;
            }

            void SelectAt(Vector2f screen_pos)
            {
                if (_editor == nullptr) return;
                _editor->_hovered_element = ElementAt(screen_pos);
                if (_editor->_hovered_element != nullptr) _editor->SetSelectedElement(_editor->_hovered_element);
                InvalidatePaint();
            }

            void DrawGrid(UI::UIRenderer &renderer, const Vector4f &content_rect) const
            {
                if (_editor == nullptr || !_editor->_show_designer_grid || _editor->_designer_zoom < 0.20f) return;
                constexpr f32 kGridSpacing = 32.0f;
                const Vector2f top_left = _editor->ScreenToDesign(content_rect.xy);
                const Vector2f bottom_right = _editor->ScreenToDesign(content_rect.xy + content_rect.zw);
                const i32 first_x = static_cast<i32>(std::floor(top_left.x / kGridSpacing));
                const i32 last_x = static_cast<i32>(std::ceil(bottom_right.x / kGridSpacing));
                const i32 first_y = static_cast<i32>(std::floor(top_left.y / kGridSpacing));
                const i32 last_y = static_cast<i32>(std::ceil(bottom_right.y / kGridSpacing));
                const Color minor(0.30f, 0.32f, 0.35f, 0.22f);
                for (i32 x = first_x; x <= last_x; ++x)
                {
                    const f32 screen_x = _editor->DesignToScreen({static_cast<f32>(x) * kGridSpacing, 0.0f}).x;
                    renderer.DrawLine({screen_x, content_rect.y}, {screen_x, content_rect.y + content_rect.w}, 1.0f, minor, -0.1f);
                }
                for (i32 y = first_y; y <= last_y; ++y)
                {
                    const f32 screen_y = _editor->DesignToScreen({0.0f, static_cast<f32>(y) * kGridSpacing}).y;
                    renderer.DrawLine({content_rect.x, screen_y}, {content_rect.x + content_rect.z, screen_y}, 1.0f, minor, -0.1f);
                }
            }

            void DrawElementOutline(UI::UIRenderer &renderer, UI::UIElement *asset_element, Color color, f32 depth) const
            {
                const Vector4f rect = GetElementScreenRect(asset_element);
                if (rect.z <= 0.0f || rect.w <= 0.0f) return;
                renderer.DrawBox(rect.xy, rect.zw, 2.0f, color, depth);
            }

            Vector2f GetAnchorScreenPosition() const
            {
                if (_editor == nullptr || _editor->_selected_element == nullptr)
                    return Vector2f::kZero;
                const auto *slot = dynamic_cast<const UI::CanvasSlot *>(_editor->_selected_element->GetSlot().get());
                UI::UIElement *parent = _editor->_selected_element->GetParent();
                if (slot == nullptr || parent == nullptr || parent->As<UI::Canvas>() == nullptr)
                    return Vector2f::kZero;
                const Vector4f parent_rect = GetElementScreenRect(parent);
                if (parent_rect.z <= 0.0f || parent_rect.w <= 0.0f)
                    return Vector2f::kZero;
                return parent_rect.xy + parent_rect.zw * slot->_anchor;
            }

            void DrawAnchorMarker(UI::UIRenderer &renderer) const
            {
                const Vector2f anchor = GetAnchorScreenPosition();
                if (anchor == Vector2f::kZero)
                    return;
                const Color color(0.30f, 0.90f, 0.55f, 1.0f);
                renderer.DrawLine({anchor.x - 6.0f, anchor.y}, {anchor.x + 6.0f, anchor.y}, 1.5f, color, 0.75f);
                renderer.DrawLine({anchor.x, anchor.y - 6.0f}, {anchor.x, anchor.y + 6.0f}, 1.5f, color, 0.75f);
            }

            void DrawAlignmentGuides(UI::UIRenderer &renderer, const Vector4f &content_rect) const
            {
                const Color color(0.95f, 0.36f, 0.72f, 0.85f);
                for (f32 x : _alignment_guides_x)
                    renderer.DrawLine({x, content_rect.y}, {x, content_rect.y + content_rect.w}, 1.0f, color, 0.65f);
                for (f32 y : _alignment_guides_y)
                    renderer.DrawLine({content_rect.x, y}, {content_rect.x + content_rect.z, y}, 1.0f, color, 0.65f);
            }

            Vector4f GetElementDesignRect(UI::UIElement *asset_element) const
            {
                const auto *source_slot = asset_element == nullptr ? nullptr :
                    dynamic_cast<const UI::CanvasSlot *>(asset_element->GetSlot().get());
                if (_editor != nullptr && source_slot != nullptr &&
                    asset_element->GetParent() == _editor->WorkingRoot())
                    return {source_slot->_position, source_slot->_size};

                UI::UIElement *preview_element = _editor == nullptr ? nullptr : _editor->ResolvePreviewElement(asset_element);
                UI::UIElement *preview_root = _editor == nullptr || _editor->_preview_widget == nullptr ? nullptr :
                    _editor->_preview_widget->Root();
                if (preview_element == nullptr || preview_root == nullptr)
                {
                    if (source_slot != nullptr && asset_element->GetParent() != nullptr &&
                        asset_element->GetParent()->As<UI::Canvas>() != nullptr)
                        return {source_slot->_position, source_slot->_size};
                    return Vector4f::kZero;
                }

                Vector4f design_rect = preview_element->GetArrangeRect();
                design_rect.xy -= preview_root->GetArrangeRect().xy;
                if (design_rect.z <= 0.0f || design_rect.w <= 0.0f)
                {
                    if (source_slot != nullptr && asset_element->GetParent() != nullptr &&
                        asset_element->GetParent()->As<UI::Canvas>() != nullptr)
                        return {source_slot->_position, source_slot->_size};
                }
                return design_rect;
            }

            bool CanEditCanvasElement(UI::UIElement *element) const
            {
                return _editor != nullptr && _editor->WorkingAsset() != nullptr && element != nullptr &&
                       element != _editor->WorkingRoot() && dynamic_cast<UI::CanvasSlot *>(element->GetSlot().get()) != nullptr;
            }

            Vector4f GetSelectedScreenRect() const
            {
                if (_editor == nullptr) return Vector4f::kZero;
                return GetElementScreenRect(_editor->_selected_element);
            }

            Vector4f GetElementScreenRect(UI::UIElement *asset_element) const
            {
                const Vector4f design_rect = GetElementDesignRect(asset_element);
                if (design_rect.z <= 0.0f || design_rect.w <= 0.0f) return Vector4f::kZero;
                return {_editor->DesignToScreen(design_rect.xy), design_rect.zw * _editor->_designer_zoom};
            }

            EWidgetDesignerHandle HitTestHandle(Vector2f screen_pos) const
            {
                if (!CanEditCanvasElement(_editor == nullptr ? nullptr : _editor->_selected_element))
                    return EWidgetDesignerHandle::kNone;
                const Vector4f rect = GetSelectedScreenRect();
                if (rect.z <= 0.0f || rect.w <= 0.0f) return EWidgetDesignerHandle::kNone;
                const Vector2f anchor = GetAnchorScreenPosition();
                if (anchor != Vector2f::kZero &&
                    UI::UIElement::IsPointInside(screen_pos, {anchor.x - 7.0f, anchor.y - 7.0f, 14.0f, 14.0f}))
                    return EWidgetDesignerHandle::kAnchor;
                const std::array<std::pair<EWidgetDesignerHandle, Vector2f>, 8> handles = {{
                    {EWidgetDesignerHandle::kTopLeft, rect.xy},
                    {EWidgetDesignerHandle::kTopRight, {rect.x + rect.z, rect.y}},
                    {EWidgetDesignerHandle::kBottomLeft, {rect.x, rect.y + rect.w}},
                    {EWidgetDesignerHandle::kBottomRight, rect.xy + rect.zw},
                    {EWidgetDesignerHandle::kLeft, {rect.x, rect.y + rect.w * 0.5f}},
                    {EWidgetDesignerHandle::kRight, {rect.x + rect.z, rect.y + rect.w * 0.5f}},
                    {EWidgetDesignerHandle::kTop, {rect.x + rect.z * 0.5f, rect.y}},
                    {EWidgetDesignerHandle::kBottom, {rect.x + rect.z * 0.5f, rect.y + rect.w}}
                }};
                const f32 half_size = kDesignerHandleSize * 0.5f;
                for (const auto &[handle, center] : handles)
                {
                    if (UI::UIElement::IsPointInside(screen_pos, {center.x - half_size, center.y - half_size,
                                                                   kDesignerHandleSize, kDesignerHandleSize}))
                        return handle;
                }
                return EWidgetDesignerHandle::kNone;
            }

            void UpdateHandleCursor(EWidgetDesignerHandle handle) const
            {
                ECursorType cursor_type = ECursorType::kArrow;
                switch (handle)
                {
                    case EWidgetDesignerHandle::kLeft:
                    case EWidgetDesignerHandle::kRight:
                        cursor_type = ECursorType::kSizeEW;
                        break;

                    case EWidgetDesignerHandle::kTop:
                    case EWidgetDesignerHandle::kBottom:
                        cursor_type = ECursorType::kSizeNS;
                        break;

                    case EWidgetDesignerHandle::kTopLeft:
                    case EWidgetDesignerHandle::kBottomRight:
                        cursor_type = ECursorType::kSizeNWSE;
                        break;

                    case EWidgetDesignerHandle::kTopRight:
                    case EWidgetDesignerHandle::kBottomLeft:
                        cursor_type = ECursorType::kSizeNESW;
                        break;

                    default:
                        return;
                }
                Application::Get().SetCursor(cursor_type, ECursorPriority::kHigh);
            }

            void DrawResizeHandles(UI::UIRenderer &renderer) const
            {
                if (!CanEditCanvasElement(_editor == nullptr ? nullptr : _editor->_selected_element)) return;
                const Vector4f rect = GetSelectedScreenRect();
                if (rect.z <= 0.0f || rect.w <= 0.0f) return;
                const std::array<Vector2f, 8> centers = {{
                    rect.xy, {rect.x + rect.z, rect.y}, {rect.x, rect.y + rect.w}, rect.xy + rect.zw,
                    {rect.x, rect.y + rect.w * 0.5f}, {rect.x + rect.z, rect.y + rect.w * 0.5f},
                    {rect.x + rect.z * 0.5f, rect.y}, {rect.x + rect.z * 0.5f, rect.y + rect.w}
                }};
                UI::UIBrush brush;
                brush._type = UI::EUIBrushType::kColor;
                brush._tint = Color(0.18f, 0.62f, 1.0f, 1.0f);
                const f32 half_size = kDesignerHandleSize * 0.5f;
                for (const Vector2f &center : centers)
                    renderer.DrawQuad({center.x - half_size, center.y - half_size, kDesignerHandleSize, kDesignerHandleSize}, brush, 0.8f);
            }

            void BeginCanvasDrag(EWidgetDesignerHandle handle, Vector2f screen_pos)
            {
                if (!CanEditCanvasElement(_editor == nullptr ? nullptr : _editor->_selected_element)) return;
                auto *slot = dynamic_cast<UI::CanvasSlot *>(_editor->_selected_element->GetSlot().get());
                if (slot == nullptr) return;
                _canvas_drag_element = _editor->_selected_element;
                _canvas_drag_handle = handle;
                _canvas_drag_start_screen = screen_pos;
                _canvas_drag_before = {slot->_position, slot->_size, slot->_anchor, slot->_size_to_content};
                _is_canvas_dragging = true;
            }

            void UpdateCanvasDrag(Vector2f screen_pos)
            {
                if (!_is_canvas_dragging || _editor == nullptr || _canvas_drag_element == nullptr) return;

                const Vector2f position_delta = _editor->ScreenToDesign(screen_pos) -
                                                _editor->ScreenToDesign(_canvas_drag_start_screen);
                Vector2f position = _canvas_drag_before._position;
                Vector2f size = _canvas_drag_before._size;
                Vector2f anchor = _canvas_drag_before._anchor;

                const bool snap = !Input::IsKeyDown(EKey::kCONTROL);
                const auto snap_value = [snap](f32 value)
                {
                    return snap ? std::round(value / kDesignerSnapSize) * kDesignerSnapSize : value;
                };

                switch (_canvas_drag_handle)
                {
                    case EWidgetDesignerHandle::kMove:
                        position += position_delta;
                        position = {snap_value(position.x), snap_value(position.y)};
                        break;

                    case EWidgetDesignerHandle::kAnchor:
                    {
                        const Vector4f parent_rect = GetElementDesignRect(_canvas_drag_element->GetParent());
                        if (parent_rect.z > 0.0f && parent_rect.w > 0.0f)
                        {
                            const Vector2f design_pos = _editor->ScreenToDesign(screen_pos);
                            anchor = {
                                std::clamp((design_pos.x - parent_rect.x) / parent_rect.z, 0.0f, 1.0f),
                                std::clamp((design_pos.y - parent_rect.y) / parent_rect.w, 0.0f, 1.0f)
                            };
                        }
                        break;
                    }

                    case EWidgetDesignerHandle::kLeft:
                        position.x = snap_value(position.x + position_delta.x);
                        size.x = snap_value(size.x - position_delta.x);
                        break;

                    case EWidgetDesignerHandle::kRight:
                        size.x = snap_value(size.x + position_delta.x);
                        break;

                    case EWidgetDesignerHandle::kTop:
                        position.y = snap_value(position.y + position_delta.y);
                        size.y = snap_value(size.y - position_delta.y);
                        break;

                    case EWidgetDesignerHandle::kBottom:
                        size.y = snap_value(size.y + position_delta.y);
                        break;

                    case EWidgetDesignerHandle::kTopLeft:
                        position.x = snap_value(position.x + position_delta.x);
                        position.y = snap_value(position.y + position_delta.y);
                        size.x = snap_value(size.x - position_delta.x);
                        size.y = snap_value(size.y - position_delta.y);
                        break;

                    case EWidgetDesignerHandle::kTopRight:
                        position.y = snap_value(position.y + position_delta.y);
                        size.x = snap_value(size.x + position_delta.x);
                        size.y = snap_value(size.y - position_delta.y);
                        break;

                    case EWidgetDesignerHandle::kBottomLeft:
                        position.x = snap_value(position.x + position_delta.x);
                        size.x = snap_value(size.x - position_delta.x);
                        size.y = snap_value(size.y + position_delta.y);
                        break;

                    case EWidgetDesignerHandle::kBottomRight:
                        size.x = snap_value(size.x + position_delta.x);
                        size.y = snap_value(size.y + position_delta.y);
                        break;

                    default:
                        break;
                }

                size.x = std::max(size.x, kMinimumCanvasSize);
                size.y = std::max(size.y, kMinimumCanvasSize);

                _editor->ApplyCanvasEdit(_canvas_drag_element, position, size, false, anchor);
                UpdateAlignmentGuides();
                InvalidatePaint();
            }

            void UpdateAlignmentGuides()
            {
                _alignment_guides_x.clear();
                _alignment_guides_y.clear();
                if (_canvas_drag_element == nullptr || _canvas_drag_element->GetParent() == nullptr)
                    return;
                constexpr f32 kGuideThreshold = 5.0f;
                const Vector4f target_rect = GetElementScreenRect(_canvas_drag_element);
                const auto add_guides = [target_rect, kGuideThreshold](const Vector4f &candidate, Vector<f32> &x_guides,
                                                                         Vector<f32> &y_guides)
                {
                    const std::array<f32, 3> target_x = {target_rect.x, target_rect.x + target_rect.z * 0.5f,
                                                          target_rect.x + target_rect.z};
                    const std::array<f32, 3> target_y = {target_rect.y, target_rect.y + target_rect.w * 0.5f,
                                                          target_rect.y + target_rect.w};
                    const std::array<f32, 3> candidate_x = {candidate.x, candidate.x + candidate.z * 0.5f,
                                                            candidate.x + candidate.z};
                    const std::array<f32, 3> candidate_y = {candidate.y, candidate.y + candidate.w * 0.5f,
                                                            candidate.y + candidate.w};
                    for (f32 value : candidate_x)
                    {
                        if (std::any_of(target_x.begin(), target_x.end(), [value](f32 target) { return std::abs(target - value) <= kGuideThreshold; }))
                            x_guides.emplace_back(value);
                    }
                    for (f32 value : candidate_y)
                    {
                        if (std::any_of(target_y.begin(), target_y.end(), [value](f32 target) { return std::abs(target - value) <= kGuideThreshold; }))
                            y_guides.emplace_back(value);
                    }
                };
                UI::UIElement *parent = _canvas_drag_element->GetParent();
                add_guides(GetElementScreenRect(parent), _alignment_guides_x, _alignment_guides_y);
                for (const Ref<UI::UIElement> &sibling : parent->GetChildren())
                {
                    if (sibling != nullptr && sibling.get() != _canvas_drag_element)
                        add_guides(GetElementScreenRect(sibling.get()), _alignment_guides_x, _alignment_guides_y);
                }
            }

            void EndCanvasDrag()
            {
                if (!_is_canvas_dragging || _editor == nullptr || _canvas_drag_element == nullptr) return;
                auto *slot = dynamic_cast<UI::CanvasSlot *>(_canvas_drag_element->GetSlot().get());
                const CanvasEditState after = slot == nullptr ? _canvas_drag_before :
                  CanvasEditState{slot->_position, slot->_size, slot->_anchor, slot->_size_to_content};
                if (!(_canvas_drag_before == after))
                    _editor->CommitCanvasEdit(_canvas_drag_element, _canvas_drag_before._position, _canvas_drag_before._size,
                                              _canvas_drag_before._size_to_content, _canvas_drag_before._anchor);
                _is_canvas_dragging = false;
                _canvas_drag_element = nullptr;
                _canvas_drag_handle = EWidgetDesignerHandle::kNone;
                _alignment_guides_x.clear();
                _alignment_guides_y.clear();
                InvalidatePaint();
            }

            void CancelCanvasDrag()
            {
                if (!_is_canvas_dragging || _editor == nullptr || _canvas_drag_element == nullptr) return;
                _editor->ApplyCanvasEdit(_canvas_drag_element, _canvas_drag_before._position, _canvas_drag_before._size,
                                         _canvas_drag_before._size_to_content, _canvas_drag_before._anchor);
                _is_canvas_dragging = false;
                _canvas_drag_element = nullptr;
                _canvas_drag_handle = EWidgetDesignerHandle::kNone;
                _alignment_guides_x.clear();
                _alignment_guides_y.clear();
                InvalidatePaint();
            }

            WidgetEditor *_editor = nullptr;
            Vector2f _last_mouse = Vector2f::kZero;
            bool _is_panning = false;
            bool _is_canvas_dragging = false;
            UI::UIElement *_canvas_drag_element = nullptr;
              EWidgetDesignerHandle _canvas_drag_handle = EWidgetDesignerHandle::kNone;
              Vector2f _canvas_drag_start_screen = Vector2f::kZero;
              CanvasEditState _canvas_drag_before;
              Vector<f32> _alignment_guides_x;
            Vector<f32> _alignment_guides_y;
            u64 _preview_mouse_move_frame = ~0ull;
            Vector2f _preview_mouse_move_pos = Vector2f::kZero;
            Vector2f _preview_mouse_move_delta = Vector2f::kZero;
        };

        class WidgetEditor::WidgetTreeDataSource final : public UI::ITreeViewDataSource
        {
        public:
            void SetRoot(UI::UIElement *root)
            {
                _root = root;
                _element_to_item.clear();
                _item_to_element.clear();
                std::unordered_set<UI::UIElement *> visited;
                RegisterElementRecursive(_root, visited);
            }

            UI::TreeItemId ToItem(UI::UIElement *element) const
            {
                const auto iter = _element_to_item.find(element);
                return iter == _element_to_item.end() ? UI::kInvalidTreeItemId : iter->second;
            }

            UI::UIElement *ToElement(UI::TreeItemId item) const
            {
                const auto iter = _item_to_element.find(item);
                return iter == _item_to_element.end() ? nullptr : iter->second;
            }

            Vector<UI::TreeItemId> GetRootItems() const override
            {
                const UI::TreeItemId root_item = ToItem(_root);
                return root_item == UI::kInvalidTreeItemId ? Vector<UI::TreeItemId>{} : Vector<UI::TreeItemId>{root_item};
            }

            Vector<UI::TreeItemId> GetChildren(UI::TreeItemId parent) const override
            {
                Vector<UI::TreeItemId> children;
                UI::UIElement *element = ToElement(parent);
                if (element == nullptr) return children;
                for (const Ref<UI::UIElement> &child : element->GetChildren())
                {
                    const UI::TreeItemId child_item = ToItem(child.get());
                    if (child_item != UI::kInvalidTreeItemId) children.push_back(child_item);
                }
                return children;
            }

            UI::TreeItemId GetParent(UI::TreeItemId item) const override
            {
                UI::UIElement *element = ToElement(item);
                return element == nullptr ? UI::kInvalidTreeItemId : ToItem(element->GetParent());
            }

            UI::TreeItemPresentation GetPresentation(UI::TreeItemId item) const override
            {
                UI::TreeItemPresentation presentation;
                UI::UIElement *element = ToElement(item);
                presentation._label = element == nullptr ? "<Invalid>" : element->Name();
                presentation._draggable = element != nullptr && element != _root;
                presentation._drop_target = element != nullptr;
                return presentation;
            }

            bool IsValid(UI::TreeItemId item) const override { return ToElement(item) != nullptr; }

        private:
            void RegisterElementRecursive(UI::UIElement *element, std::unordered_set<UI::UIElement *> &visited)
            {
                if (element == nullptr || !visited.insert(element).second) return;
                const Guid &guid = element->GuidValue();
                auto guid_iter = guid.IsEmpty() ? _guid_to_item.end() : _guid_to_item.find(guid);
                if (guid_iter == _guid_to_item.end() || _item_to_element.contains(guid_iter->second))
                {
                    const UI::TreeItemId item = _next_item++;
                    if (!guid.IsEmpty())
                        _guid_to_item.emplace(guid, item);
                    _element_to_item.emplace(element, item);
                    _item_to_element.emplace(item, element);
                }
                else
                {
                    const UI::TreeItemId item = guid_iter->second;
                    _element_to_item.emplace(element, item);
                    _item_to_element.emplace(item, element);
                }
                for (const Ref<UI::UIElement> &child : element->GetChildren())
                    RegisterElementRecursive(child.get(), visited);
            }

            UI::UIElement *_root = nullptr;
            UI::TreeItemId _next_item = 1u;
            std::unordered_map<Guid, UI::TreeItemId, GuidHasher> _guid_to_item;
            std::unordered_map<UI::UIElement *, UI::TreeItemId> _element_to_item;
            std::unordered_map<UI::TreeItemId, UI::UIElement *> _item_to_element;
        };

        WidgetEditor::WidgetEditor() : AssetEditor("Widget Editor", {1200.0f, 800.0f})
        {
            SetPosition({120.0f, 60.0f});
            _tree_data_source = MakeScope<WidgetTreeDataSource>();
            _element_property_panel = MakeScope<ReflectedPropertyPanel>();
            _slot_property_panel = MakeScope<ReflectedPropertyPanel>();
            BuildUi();
            RefreshDirtyState();
        }

        WidgetEditor::~WidgetEditor()
        {
            DestroyPreview();
        }

        void WidgetEditor::Update(f32 dt)
        {
            AssetEditor::Update(dt);
            if (_asset == nullptr)
                return;
            RefreshWorkingState();
            FlushPendingPropertyEdit();
            FlushDeferredPropertyEdit();
            RefreshPendingCanvasEdit();
            RefreshPendingPropertyEdit();
            if (_preview_rebuild_pending)
            {
                _preview_rebuild_pending = false;
                RebuildPreview();
                RefreshDesigner();
            }
            if (_is_preview_mode && Input::IsKeyJustPressed(EKey::kESCAPE))
            {
                SetPreviewMode(false);
                return;
            }
            if (_is_preview_mode)
                return;
            const bool ctrl = Input::IsKeyDown(EKey::kLCONTROL) || Input::IsKeyDown(EKey::kRCONTROL) ||
                              Input::IsKeyDown(EKey::kCONTROL);
            if (ctrl && Input::IsKeyDownAccurate(EKey::kZ) && g_pCommandMgr != nullptr) g_pCommandMgr->Undo();
            if (ctrl && Input::IsKeyDownAccurate(EKey::kY) && g_pCommandMgr != nullptr) g_pCommandMgr->Redo();
            if (ctrl && Input::IsKeyDownAccurate(EKey::kD)) DuplicateSelectedElement();
            if (Input::IsKeyDownAccurate(EKey::kF)) FitDesigner();
            if (Input::IsKeyDownAccurate(EKey::k1))
            {
                _designer_zoom = 1.0f;
                RefreshDesigner();
            }
            if (_selected_element == nullptr)
                return;
            if (Input::IsKeyDownAccurate(EKey::kDELETE))
                DeleteSelectedElement();
            if (Input::IsKeyDownAccurate(EKey::kF2))
                RenameSelectedElement(Input::GetGlobalMousePos());
        }

        void WidgetEditor::Open(WidgetAsset *asset)
        {
            BindAsset(ResourceMgr::Get().GetLinkedAsset(asset));
            SetPreviewMode(false);
            DestroyPreview();
            _asset = asset;
            _working_asset = _asset == nullptr ? nullptr : MakeRef<WidgetAsset>(_asset->Name());
            if (_working_asset != nullptr)
            {
                _working_asset->SetDesignSize(_asset->DesignSize());
                _working_asset->SetRoot(CloneUIElementTree(_asset->RootRef()));
                EnsureUniqueWidgetGuids(_working_asset->Root());
                _saved_root = CloneUIElementTree(_working_asset->RootRef());
                _saved_design_size = _working_asset->DesignSize();
                _edit_session = MakeRef<WidgetEditSession>();
                _edit_session->_working_asset = _working_asset;
            }
            _preview_rebuild_pending = false;
            _selected_element = WorkingRoot();
            _selected_guid = _selected_element == nullptr ? Guid::EmptyGuid() : _selected_element->GuidValue();
            if (_edit_session != nullptr)
                _edit_session->_selected_guid = _selected_guid;
            _hovered_element = nullptr;
            _designer_needs_fit = true;
            RefreshPanels();
            RefreshDirtyState();
        }

        void WidgetEditor::OnClose()
        {
            SetPreviewMode(false);
            DestroyPreview();
            _asset = nullptr;
            _working_asset.reset();
            _saved_root.reset();
            _edit_session.reset();
            _preview_rebuild_pending = false;
            _selected_element = nullptr;
            _selected_guid = Guid::EmptyGuid();
            _hovered_element = nullptr;
            _preview_capture_element = nullptr;
            _preview_popup_widget = nullptr;
            _tree_data_source->SetRoot(nullptr);
            _hierarchy_tree->SetDataSource(nullptr);
            RefreshPanels();
            RefreshDirtyState();
        }

        void WidgetEditor::OnBeforeSave()
        {
            if (_asset == nullptr || _working_asset == nullptr)
                return;

            SyncRuntimeFromWorking();
            if (GetAsset() != nullptr && !GetAsset()->IsDirty())
                GetAsset()->MarkModified();
        }

        void WidgetEditor::OnAssetSaved()
        {
            if (_working_asset == nullptr)
                return;
            _saved_root = CloneUIElementTree(_working_asset->RootRef());
            _saved_design_size = _working_asset->DesignSize();
            RefreshDirtyState();
        }

        void WidgetEditor::OnAssetReloaded()
        {
            _asset = GetAssetObject<WidgetAsset>();
            if (_asset != nullptr)
                Open(_asset);
        }

        void WidgetEditor::MarkDirty()
        {
            if (_asset == nullptr)
                return;
            if (IsDetailValueDragActive())
            {
                if (GetAsset() != nullptr && !GetAsset()->IsDirty())
                    GetAsset()->MarkModified();
                RefreshDirtyState();
                return;
            }
            if (IsWorkingTreeSaved())
            {
                if (GetAsset() != nullptr && GetAsset()->IsDirty())
                    GetAsset()->RestoreRevision(GetAsset()->GetSavedRevision());
            }
            else
            {
                SyncRuntimeFromWorking();
                if (GetAsset() != nullptr && !GetAsset()->IsDirty())
                    GetAsset()->MarkModified();
            }
            RefreshDirtyState();
        }

        void WidgetEditor::SyncRuntimeFromWorking()
        {
            if (_asset == nullptr || _working_asset == nullptr)
                return;
            if (_asset->DesignSize() == _working_asset->DesignSize() &&
                SerializeWidgetTree(_asset->RootRef()) == SerializeWidgetTree(_working_asset->RootRef()))
                return;
            _asset->SetDesignSize(_working_asset->DesignSize());
            _asset->SetRoot(CloneUIElementTree(_working_asset->RootRef()));
        }

        UI::UIElement *WidgetEditor::WorkingRoot() const
        {
            return _working_asset == nullptr ? nullptr : _working_asset->Root();
        }

        Ref<UI::UIElement> WidgetEditor::CloneWorkingRoot() const
        {
            return _working_asset == nullptr ? nullptr : CloneUIElementTree(_working_asset->RootRef());
        }

        bool WidgetEditor::IsWorkingTreeSaved() const
        {
            if (_working_asset == nullptr)
                return true;
            return _working_asset->DesignSize() == _saved_design_size &&
                   SerializeWidgetTree(_working_asset->RootRef()) == SerializeWidgetTree(_saved_root);
        }

        void WidgetEditor::ApplyWorkingSnapshot(const Ref<UI::UIElement> &root, const Guid &selected_guid)
        {
            if (_working_asset == nullptr)
                return;
            _working_asset->SetRoot(CloneUIElementTree(root));
            _selected_element = _working_asset->Root() == nullptr ? nullptr :
                _working_asset->Root()->FindChildByGuid(selected_guid);
            if (_selected_element == nullptr)
                _selected_element = _working_asset->Root();
            SetSelectedElement(_selected_element);
            _preview_rebuild_pending = true;
            RefreshPanels();
            MarkDirty();
        }

        void WidgetEditor::CommitTreeEdit(Ref<UI::UIElement> before_root, const Guid &before_selection)
        {
            if (_working_asset == nullptr || before_root == nullptr)
                return;
            Ref<UI::UIElement> after_root = CloneWorkingRoot();
            if (after_root == nullptr)
                return;
            const Guid after_selection = _selected_element == nullptr ? Guid::EmptyGuid() : _selected_element->GuidValue();
            if (g_pCommandMgr != nullptr && _edit_session != nullptr)
            {
                g_pCommandMgr->ExecuteCommand(std::make_unique<WidgetTreeEditCommand>(
                    _edit_session, std::move(before_root), std::move(after_root), before_selection, after_selection));
                _edit_session->_selected_guid = after_selection;
                _edit_session->_refresh_pending = true;
                RefreshWorkingState();
                return;
            }
            if (_edit_session != nullptr)
            {
                _edit_session->_selected_guid = after_selection;
                _edit_session->_refresh_pending = true;
            }
            RefreshWorkingState();
        }

        void WidgetEditor::RefreshWorkingState()
        {
            if (_edit_session == nullptr || !_edit_session->_refresh_pending)
                return;
            _edit_session->_refresh_pending = false;
            UI::UIElement *selected = WorkingRoot() == nullptr ? nullptr :
                WorkingRoot()->FindChildByGuid(_edit_session->_selected_guid);
            SetSelectedElement(selected == nullptr ? WorkingRoot() : selected);
            _preview_rebuild_pending = true;
            RefreshHierarchy();
            RefreshDesigner();
            MarkDirty();
        }

        void WidgetEditor::SetSelectedElement(UI::UIElement *element, bool sync_hierarchy)
        {
            _selected_element = element;
            _selected_guid = element == nullptr ? Guid::EmptyGuid() : element->GuidValue();
            if (_edit_session != nullptr)
                _edit_session->_selected_guid = _selected_guid;
            RefreshDetails();
            if (_designer_preview != nullptr) _designer_preview->InvalidatePaint();
            if (!sync_hierarchy || _hierarchy_tree == nullptr) return;
            const UI::TreeItemId item = _tree_data_source->ToItem(_selected_element);
            if (item == UI::kInvalidTreeItemId)
                _hierarchy_tree->ClearSelection(false);
            else
            {
                _hierarchy_tree->ExpandParents(item);
                _hierarchy_tree->SetSelectedItem(item, false);
                _hierarchy_tree->ScrollItemIntoView(item);
            }
        }

        void WidgetEditor::BuildUi()
        {
            auto *root = _content_root->AddChild<UI::VerticalBox>();
            root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *toolbar = root->AddChild<UI::HorizontalBox>();
            toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                   .Size({0.0f, kToolbarHeight});
            BuildToolbar(toolbar);

            auto *main_area = root->AddChild<UI::SplitView>();
            main_area->_is_horizontal = true;
            main_area->SetRatio(kLeftPanelRatio);
            main_area->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *left_split = main_area->AddChild<UI::SplitView>();
            left_split->_is_horizontal = false;
            left_split->SetRatio(0.50f);
            auto *palette_panel = AddPanel(left_split, "Palette", Color(0.16f, 0.17f, 0.19f, 1.0f));
            auto *palette_search = palette_panel->AddChild<UI::InputBlock>("Search Elements...");
            palette_search->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                          .Size({0.0f, 26.0f}).Margin({0.0f, 0.0f, 0.0f, 4.0f});
            auto *palette_scroll_view = palette_panel->AddChild<UI::ScrollView>();
            palette_scroll_view->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            auto *palette_content = palette_scroll_view->AddChild<UI::VerticalBox>();
            palette_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            BuildPalette(palette_content);
            palette_search->_on_content_changed += [this, palette_content, palette_scroll_view](String filter)
            {
                palette_content->ClearChildren();
                palette_scroll_view->ResetScrollOffset();
                BuildPalette(palette_content, filter);
            };

            auto *hierarchy_panel = AddPanel(left_split, "Hierarchy", Color(0.16f, 0.17f, 0.19f, 1.0f));
            _hierarchy_content = hierarchy_panel->AddChild<UI::VerticalBox>();
            _hierarchy_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _hierarchy_tree = _hierarchy_content->AddChild<UI::TreeView>();
            _hierarchy_tree->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _hierarchy_tree->SetDataSource(_tree_data_source.get());
            _hierarchy_tree->_on_selection_changed += [this](UI::TreeItemId item)
            {
                SetSelectedElement(_tree_data_source->ToElement(item), false);
            };
            _hierarchy_tree->_on_item_context_menu += [this](UI::TreeItemId item, Vector2f pos)
            {
                ShowHierarchyContextMenu(item, pos);
            };
            _hierarchy_tree->SetCanDragCallback([this](UI::TreeItemId item)
            {
                return WorkingAsset() != nullptr && _tree_data_source->ToElement(item) != WorkingRoot();
            });
            _hierarchy_tree->SetCanDropCallback([this](UI::TreeView *source_tree, UI::TreeItemId source,
                                                        UI::TreeItemId target)
            {
                if (source_tree != _hierarchy_tree)
                    return false;
                UI::UIElement *new_parent = target == UI::kInvalidTreeItemId ? WorkingRoot() :
                    _tree_data_source->ToElement(target);
                return CanReparent(_tree_data_source->ToElement(source), new_parent);
            });
            _hierarchy_tree->SetDropCallback([this](UI::TreeView *source_tree, UI::TreeItemId source,
                                                     UI::TreeItemId target)
            {
                if (source_tree == _hierarchy_tree)
                {
                    UI::UIElement *new_parent = target == UI::kInvalidTreeItemId ? WorkingRoot() :
                        _tree_data_source->ToElement(target);
                    ReparentElement(_tree_data_source->ToElement(source), new_parent);
                }
            });
            _hierarchy_tree->SetCanDropAtCallback([this](UI::TreeView *source_tree, UI::TreeItemId source,
                                                          UI::TreeItemId target, UI::ETreeDropLocation location)
            {
                if (source_tree != _hierarchy_tree || target == UI::kInvalidTreeItemId)
                    return false;
                UI::UIElement *source_element = _tree_data_source->ToElement(source);
                UI::UIElement *target_element = _tree_data_source->ToElement(target);
                if (location == UI::ETreeDropLocation::kOnItem)
                    return CanReparent(source_element, target_element);
                return CanInsertElement(source_element, target_element, location == UI::ETreeDropLocation::kBeforeItem);
            });
            _hierarchy_tree->SetDropAtCallback([this](UI::TreeView *source_tree, UI::TreeItemId source,
                                                       UI::TreeItemId target, UI::ETreeDropLocation location)
            {
                if (source_tree != _hierarchy_tree || target == UI::kInvalidTreeItemId)
                    return;
                UI::UIElement *source_element = _tree_data_source->ToElement(source);
                UI::UIElement *target_element = _tree_data_source->ToElement(target);
                if (location == UI::ETreeDropLocation::kOnItem)
                    ReparentElement(source_element, target_element);
                else
                    InsertElement(source_element, target_element, location == UI::ETreeDropLocation::kBeforeItem);
            });
            _hierarchy_tree->SetExternalCanDropCallback([this](const UI::DragPayload &payload, UI::TreeItemId target)
            {
                if (payload._type != UI::EDragType::kUIWidget || payload._data == nullptr ||
                    target == UI::kInvalidTreeItemId)
                    return false;
                const UI::UIElement *parent = _tree_data_source->ToElement(target);
                return CanAcceptChild(parent, nullptr);
            });
            _hierarchy_tree->SetExternalDropCallback([this](const UI::DragPayload &payload, UI::TreeItemId target, Vector2f)
            {
                if (payload._type != UI::EDragType::kUIWidget || payload._data == nullptr ||
                    target == UI::kInvalidTreeItemId)
                    return;
                AddPaletteElement(static_cast<const Type *>(payload._data), _tree_data_source->ToElement(target), Vector2f::kZero);
            });

            auto *content_split = main_area->AddChild<UI::SplitView>();
            content_split->_is_horizontal = true;
            content_split->SetRatio(kCenterPanelRatio);

            auto *designer = AddPanel(content_split, "Designer", Color(0.10f, 0.11f, 0.12f, 1.0f));
            _asset_name = designer->AddChild<UI::Text>("No Widget Asset");
            _design_size = designer->AddChild<UI::Text>("Design Size: -");
            _preview_size = designer->AddChild<UI::Text>("Preview Size: -");
            auto *designer_toolbar = designer->AddChild<UI::HorizontalBox>();
            designer_toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                            .Size({0.0f, 26.0f});
            _fit_button = designer_toolbar->AddChild<UI::Button>("Fit");
            _fit_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                       .Size({44.0f, 0.0f}).Margin({0.0f, 0.0f, 8.0f, 0.0f});
            _fit_button->OnMouseClick() += [this](UI::UIEvent &event)
            {
                FitDesigner();
                event._is_handled = true;
            };
            designer_toolbar->AddChild<UI::Text>("W")->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({16.0f, 0.0f});
            _design_width_input = designer_toolbar->AddChild<UI::InputBlock>("1920");
            _design_width_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size({54.0f, 0.0f}).Margin({0.0f, 0.0f, 4.0f, 0.0f});
            designer_toolbar->AddChild<UI::Text>("H")->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({16.0f, 0.0f});
            _design_height_input = designer_toolbar->AddChild<UI::InputBlock>("1080");
            _design_height_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size({54.0f, 0.0f}).Margin({0.0f, 0.0f, 8.0f, 0.0f});
            const auto change_design_size = [this](bool is_width, const String &value)
            {
                if (WorkingAsset() == nullptr)
                    return;
                const f32 dimension = std::clamp(static_cast<f32>(std::atof(value.c_str())), 1.0f, 16384.0f);
                Vector2f size = WorkingAsset()->DesignSize();
                if (is_width)
                    size.x = dimension;
                else
                    size.y = dimension;
                if (size == WorkingAsset()->DesignSize())
                    return;
                WorkingAsset()->SetDesignSize(size);
                _designer_needs_fit = true;
                _preview_rebuild_pending = true;
                MarkDirty();
                RefreshDesigner();
            };
            _design_width_input->_on_content_changed += [change_design_size](String value) { change_design_size(true, value); };
            _design_height_input->_on_content_changed += [change_design_size](String value) { change_design_size(false, value); };
            _grid_checkbox = designer_toolbar->AddChild<UI::CheckBox>();
            _grid_checkbox->SetChecked(_show_designer_grid);
            _grid_checkbox->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                          .Size({22.0f, 0.0f});
            _grid_checkbox->OnMouseClick() += [this](UI::UIEvent &event)
            {
                _show_designer_grid = _grid_checkbox->IsChecked();
                if (_designer_preview != nullptr) _designer_preview->InvalidatePaint();
                event._is_handled = true;
            };
            designer_toolbar->AddChild<UI::Text>("Grid")->GetSlotAs<UI::LinearSlot>()
                .SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill).Size({32.0f, 0.0f});
            _designer_zoom_label = designer_toolbar->AddChild<UI::Text>("100%");
            _designer_zoom_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                                .Size({52.0f, 0.0f});
            _designer_label = designer_toolbar->AddChild<UI::Text>("Open a Widget Asset to begin designing.");
            _designer_label->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _designer_preview = designer->AddChild<WidgetDesignerPreview>(this);
            _designer_preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);

            auto *details_panel = AddPanel(content_split, "Details", Color(0.16f, 0.17f, 0.19f, 1.0f));
            _details_scroll_view = details_panel->AddChild<UI::ScrollView>();
            _details_scroll_view->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _details_content = _details_scroll_view->AddChild<UI::VerticalBox>();
            _details_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);

            auto *status_bar = root->AddChild<UI::HorizontalBox>();
            status_bar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                      .Size({0.0f, kStatusBarHeight});
            status_bar->SlotPadding() = UI::Padding(8.0f, 2.0f, 8.0f, 2.0f);
            _status_text = status_bar->AddChild<UI::Text>("No Widget Asset");
        }

        void WidgetEditor::BuildToolbar(UI::HorizontalBox *toolbar)
        {
            toolbar->SlotPadding() = UI::Padding(8.0f, 4.0f, 8.0f, 4.0f);
            _save_button = toolbar->AddChild<UI::Button>("Save");
            _save_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size({56.0f, 0.0f}).Margin({0.0f, 0.0f, 8.0f, 0.0f});
            _save_button->OnMouseClick() += [this](UI::UIEvent &event)
            {
                Save();
                event._is_handled = true;
            };
            _undo_button = toolbar->AddChild<UI::Button>("Undo");
            _undo_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size({56.0f, 0.0f}).Margin({0.0f, 0.0f, 8.0f, 0.0f});
            _undo_button->OnMouseClick() += [](UI::UIEvent &event)
            {
                if (g_pCommandMgr != nullptr) g_pCommandMgr->Undo();
                event._is_handled = true;
            };

            _redo_button = toolbar->AddChild<UI::Button>("Redo");
            _redo_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                        .Size({56.0f, 0.0f}).Margin({0.0f, 0.0f, 8.0f, 0.0f});
            _redo_button->OnMouseClick() += [](UI::UIEvent &event)
            {
                if (g_pCommandMgr != nullptr) g_pCommandMgr->Redo();
                event._is_handled = true;
            };

            _design_mode_button = toolbar->AddChild<UI::Button>("Design");
            _design_mode_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                                 .Size({72.0f, 0.0f}).Margin({0.0f, 0.0f, 8.0f, 0.0f});
            _design_mode_button->OnMouseClick() += [this](UI::UIEvent &event)
            {
                SetPreviewMode(false);
                event._is_handled = true;
            };
            _preview_mode_button = toolbar->AddChild<UI::Button>("Preview");
            _preview_mode_button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                                  .Size({72.0f, 0.0f});
            _preview_mode_button->OnMouseClick() += [this](UI::UIEvent &event)
            {
                SetPreviewMode(true);
                event._is_handled = true;
            };
            toolbar->AddChild<UI::Text>("")->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            RefreshModeState();
        }

        void WidgetEditor::BuildPalette(UI::VerticalBox *parent, const String &filter)
        {
            if (parent == nullptr) return;
            const String normalized_filter = TrimName(filter);
            const Type *ui_element_type = StaticClass<UI::UIElement>();
            Vector<const Type *> basic_types;
            Vector<const Type *> layout_types;
            for (const Type *type: Type::GetAllTypes())
            {
                if (type == ui_element_type || !type->IsClass() || type->IsAbstract() || !type->CanCreateInstance() ||
                    !IsTypeDerivedFrom(type, ui_element_type) ||
                    (!ContainsInsensitive(type->Name(), normalized_filter) &&
                     !ContainsInsensitive(type->FullName(), normalized_filter)))
                    continue;
                (IsLayoutElementType(type) ? layout_types : basic_types).emplace_back(type);
            }

            const auto add_category = [parent](const String &category, const Vector<const Type *> &types)
            {
                if (types.empty()) return;
                parent->AddChild<UI::Text>(category);
                for (const Type *type: types)
                {
                    auto *button = parent->AddChild<UI::Button>(type->Name());
                    button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                          .Size({0.0f, 24.0f});
                    button->OnMouseDown() += [type](UI::UIEvent &event)
                    {
                        UI::DragDropManager::Get().BeginDrag({UI::EDragType::kUIWidget, const_cast<Type *>(type)},
                                                             type->Name());
                        event._is_handled = true;
                    };
                }
            };
            add_category("Basic", basic_types);
            add_category("Layout", layout_types);
        }

        void WidgetEditor::RefreshPanels()
        {
            RebuildPreview();
            RefreshHierarchy();
            RefreshDesigner();
            RefreshDetails();
        }

        void WidgetEditor::RefreshHierarchy()
        {
            _tree_data_source->SetRoot(WorkingRoot());
            UI::ITreeViewDataSource *data_source = WorkingAsset() != nullptr ? _tree_data_source.get() : nullptr;
            if (_hierarchy_tree->GetDataSource() != data_source)
                _hierarchy_tree->SetDataSource(data_source);
            else if (data_source != nullptr)
                _hierarchy_tree->Refresh();
            SetSelectedElement(_selected_element, true);
        }

        void WidgetEditor::RefreshDetails()
        {
            if (_details_scroll_view != nullptr) _details_scroll_view->ResetScrollOffset();
            _details_content->ClearChildren();
            _element_property_panel->Clear();
            _slot_property_panel->Clear();
            if (WorkingAsset() == nullptr || _selected_element == nullptr)
            {
                _details_content->AddChild<UI::Text>("No selection");
                return;
            }
            _details_content->AddChild<UI::Text>("Selected Element");
            _details_content->AddChild<UI::Text>("Name: " + _selected_element->Name());
            _details_content->AddChild<UI::Text>("Type: " + _selected_element->GetType()->Name());
            _details_content->AddChild<UI::Text>("Element Properties");
            _element_property_panel->Build({
                _selected_element->GetType(), _selected_element, _details_content, IsEditorVisibleProperty,
                [this, element = _selected_element](const PropertyInfo &property)
                {
                    OnPropertyChanging(property, element, element);
                },
                [this, element = _selected_element](const PropertyInfo &property)
                {
                    OnPropertyChanged(property, element, element);
                }
            });

            UI::UISlot *slot = _selected_element->GetSlot().get();
            if (slot == nullptr || slot->GetType() == nullptr)
                return;
            _details_content->AddChild<UI::Text>("Slot Properties");
            _slot_property_panel->Build({
                slot->GetType(), slot, _details_content, IsEditorVisibleProperty,
                [this, slot, element = _selected_element](const PropertyInfo &property)
                {
                    OnPropertyChanging(property, slot, element);
                },
                [this, slot, element = _selected_element](const PropertyInfo &property)
                {
                    OnPropertyChanged(property, slot, element);
                }
            });
        }

        void WidgetEditor::OnPropertyChanging(const PropertyInfo &property, void *instance, UI::UIElement *owner)
        {
            _pending_property_snapshot = {&property, instance, owner, SerializePropertyValue(property, instance),
                                          instance == (owner == nullptr ? nullptr : owner->GetSlot().get())};
        }

        void WidgetEditor::OnPropertyChanged(const PropertyInfo &property, void *instance, UI::UIElement *owner)
        {
            if (_pending_property_snapshot._property != &property || _pending_property_snapshot._instance != instance)
                return;
            FlushPendingPropertyEdit();
        }

        void WidgetEditor::FlushPendingPropertyEdit()
        {
            if (_pending_property_snapshot._property == nullptr || _pending_property_snapshot._instance == nullptr)
                return;
            const PropertyInfo &property = *_pending_property_snapshot._property;
            void *instance = _pending_property_snapshot._instance;
            UI::UIElement *owner = _pending_property_snapshot._owner;
            const String after = SerializePropertyValue(property, instance);
            const bool changed = _pending_property_snapshot._before != after;
            String before = std::move(_pending_property_snapshot._before);
            const bool is_slot = _pending_property_snapshot._is_slot;
            _pending_property_snapshot = {};
            if (owner != nullptr)
            {
                if (is_slot)
                    InvalidateLayoutToRoot(owner);
                else
                    owner->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
            }
            if (changed)
            {
                SyncPreviewProperty(owner, property.Name(), is_slot, after);
                QueuePropertyEdit(owner, property.Name(), is_slot, std::move(before), after);
            }
        }

        void WidgetEditor::ApplyCanvasEdit(UI::UIElement *element, Vector2f position, Vector2f size, bool size_to_content,
                                           Vector2f anchor)
        {
            if (WorkingAsset() == nullptr || element == nullptr) return;
            auto *slot = dynamic_cast<UI::CanvasSlot *>(element->GetSlot().get());
            // Keep the source slot on the regular reflected-property path so all property semantics stay intact.
            // CommitTreeEdit performs tree serialization and panel refresh once on mouse-up.
            SetCanvasSlotValues(slot, position, size, size_to_content, anchor, true);
            if (auto *preview_element = ResolvePreviewElement(element); preview_element != nullptr)
            {
                auto *preview_slot = dynamic_cast<UI::CanvasSlot *>(preview_element->GetSlot().get());
                SetCanvasSlotValues(preview_slot, position, size, size_to_content, anchor, false);
                _preview_widget->Update(0.0f);
            }
        }

        void WidgetEditor::CommitCanvasEdit(UI::UIElement *element, Vector2f before_position, Vector2f before_size,
                                            bool before_size_to_content, Vector2f before_anchor)
        {
            if (element == nullptr || _edit_session == nullptr)
                return;
            auto *slot = dynamic_cast<UI::CanvasSlot *>(element->GetSlot().get());
            if (slot == nullptr)
                return;
            const CanvasEditState before = {before_position, before_size, before_anchor, before_size_to_content};
            const CanvasEditState after = {slot->_position, slot->_size, slot->_anchor, slot->_size_to_content};
            if (before == after)
                return;
            if (g_pCommandMgr != nullptr)
            {
                g_pCommandMgr->ExecuteCommand(std::make_unique<WidgetCanvasEditCommand>(
                    _edit_session, element->GuidValue(), before, after));
            }
            MarkDirty();
        }

        void WidgetEditor::RefreshPendingCanvasEdit()
        {
            if (_edit_session == nullptr || !_edit_session->_canvas_refresh_pending)
                return;
            _edit_session->_canvas_refresh_pending = false;
            UI::UIElement *element = WorkingRoot() == nullptr ? nullptr :
                WorkingRoot()->FindChildByGuid(_edit_session->_canvas_element_guid);
            UI::UIElement *preview_element = ResolvePreviewElement(element);
            auto *preview_slot = preview_element == nullptr ? nullptr :
                dynamic_cast<UI::CanvasSlot *>(preview_element->GetSlot().get());
            if (preview_slot != nullptr)
            {
                const CanvasEditState &state = _edit_session->_canvas_edit_state;
                SetCanvasSlotValues(preview_slot, state._position, state._size, state._size_to_content, state._anchor, false);
                _preview_widget->Update(0.0f);
            }
            if (_designer_preview != nullptr)
                _designer_preview->InvalidatePaint();
            MarkDirty();
        }

        void WidgetEditor::CommitPropertyEdit(UI::UIElement *element, const String &property_name, bool is_slot,
                                              String before, String after)
        {
            if (element == nullptr || _edit_session == nullptr || before == after)
                return;
            if (g_pCommandMgr != nullptr)
            {
                g_pCommandMgr->ExecuteCommand(std::make_unique<WidgetPropertyEditCommand>(
                    _edit_session, element->GuidValue(), property_name, is_slot, std::move(before), std::move(after)));
            }
            MarkDirty();
        }

        bool WidgetEditor::IsDetailValueDragActive() const
        {
            const auto has_dragging_input = [](const auto &self, const UI::UIElement *element) -> bool
            {
                if (element == nullptr)
                    return false;
                if (const auto *color_picker = dynamic_cast<const UI::ColorPicker *>(element); color_picker != nullptr &&
                    color_picker->IsDragAdjusting())
                    return true;
                if (const auto *input = dynamic_cast<const UI::InputBlock *>(element); input != nullptr &&
                    input->IsDragAdjusting())
                    return true;
                for (const Ref<UI::UIElement> &child : element->GetChildren())
                {
                    if (self(self, child.get()))
                        return true;
                }
                return false;
            };
            if (has_dragging_input(has_dragging_input, _details_content))
                return true;
            UI::Widget *popup_widget = UI::UIManager::Get() == nullptr ? nullptr : UI::UIManager::Get()->GetPopupWidget();
            return popup_widget != nullptr && has_dragging_input(has_dragging_input, popup_widget->Root());
        }

        void WidgetEditor::QueuePropertyEdit(UI::UIElement *element, const String &property_name, bool is_slot,
                                             String before, String after)
        {
            if (element == nullptr || !IsDetailValueDragActive())
            {
                FlushDeferredPropertyEdit();
                CommitPropertyEdit(element, property_name, is_slot, std::move(before), std::move(after));
                return;
            }
            if (_deferred_property_edit._element_guid != element->GuidValue() ||
                _deferred_property_edit._property_name != property_name || _deferred_property_edit._is_slot != is_slot)
            {
                _deferred_property_edit = {element->GuidValue(), property_name, std::move(before), std::move(after), is_slot};
            }
            else
            {
                _deferred_property_edit._after = std::move(after);
            }
            MarkDirty();
        }

        void WidgetEditor::FlushDeferredPropertyEdit()
        {
            if (_deferred_property_edit._element_guid.IsEmpty() || IsDetailValueDragActive())
                return;
            DeferredPropertyEdit edit = std::move(_deferred_property_edit);
            _deferred_property_edit = {};
            UI::UIElement *element = WorkingRoot() == nullptr ? nullptr :
                WorkingRoot()->FindChildByGuid(edit._element_guid);
            if (edit._before != edit._after)
                CommitPropertyEdit(element, edit._property_name, edit._is_slot, std::move(edit._before), std::move(edit._after));
            else
                MarkDirty();
        }

        void WidgetEditor::RefreshPendingPropertyEdit()
        {
            if (_edit_session == nullptr || !_edit_session->_property_refresh_pending)
                return;
            _edit_session->_property_refresh_pending = false;
            UI::UIElement *element = WorkingRoot() == nullptr ? nullptr :
                WorkingRoot()->FindChildByGuid(_edit_session->_property_element_guid);
            SyncPreviewProperty(element, _edit_session->_property_name, _edit_session->_property_is_slot,
                                _edit_session->_property_snapshot);
            MarkDirty();
        }

        void WidgetEditor::SyncPreviewProperty(UI::UIElement *element, const String &property_name, bool is_slot,
                                               const String &snapshot)
        {
            UI::UIElement *preview_element = ResolvePreviewElement(element);
            void *preview_instance = is_slot && preview_element != nullptr ?
                static_cast<void *>(preview_element->GetSlot().get()) : static_cast<void *>(preview_element);
            const Type *type = preview_instance == nullptr ? nullptr :
                (is_slot ? static_cast<UI::UISlot *>(preview_instance)->GetType() : preview_element->GetType());
            PropertyInfo *property = FindPropertyInHierarchy(type, property_name);
            if (property == nullptr)
                return;
            JsonArchive archive;
            if (!archive.LoadFromString(snapshot))
                return;
            property->Deserialize(preview_instance, archive);
            property->NotifyObject(static_cast<Object *>(preview_instance), PropertyInfo::EPropertyChangeSource::kUI);
            if (is_slot)
                InvalidateLayoutToRoot(preview_element);
            else
                preview_element->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
            _preview_widget->Update(0.0f);
            if (_designer_preview != nullptr)
                _designer_preview->InvalidatePaint();
        }

        void WidgetEditor::AddPaletteElement(const Type *element_type, UI::UIElement *parent, Vector2f design_position)
        {
            if (WorkingAsset() == nullptr || parent == nullptr || element_type == nullptr) return;
            UI::UIElement *element = element_type->CreateInstance<UI::UIElement>();
            if (element == nullptr) return;
            if (!CanAcceptChild(parent, element))
                parent = WorkingRoot();
            if (!CanAcceptChild(parent, element))
                return;
            Ref<UI::UIElement> before_root = CloneWorkingRoot();
            const Guid before_selection = _selected_guid;
            element->Name(element_type->Name());
            element = parent->AddChild(Ref<UI::UIElement>(element));
            if (element == nullptr) return;

            Vector2f default_size = {160.0f, 40.0f};
            if (element->As<UI::Image>() != nullptr)
                default_size = {128.0f, 128.0f};
            else if (IsLayoutElementType(element_type))
                default_size = {320.0f, 180.0f};

            if (auto *slot = dynamic_cast<UI::CanvasSlot *>(element->GetSlot().get()); slot != nullptr)
            {
                UI::UIElement *preview_parent = ResolvePreviewElement(parent);
                UI::UIElement *preview_root = _preview_widget == nullptr ? nullptr : _preview_widget->Root();
                Vector2f parent_position = Vector2f::kZero;
                if (preview_parent != nullptr && preview_root != nullptr)
                    parent_position = preview_parent->GetArrangeRect().xy - preview_root->GetArrangeRect().xy;
                slot->Position(design_position - parent_position).Size(default_size);
            }
            element->InvalidateLayout(true);
            SetSelectedElement(element);
            CommitTreeEdit(std::move(before_root), before_selection);
        }

        void WidgetEditor::RefreshDesigner()
        {
            if (WorkingAsset() == nullptr)
            {
                _asset_name->SetText("No Widget Asset");
                _design_size->SetText("Design Size: -");
                _preview_size->SetText("Preview Size: -");
                _designer_label->SetText("Open a Widget Asset to begin designing.");
                _designer_zoom_label->SetText("-");
                return;
            }

            const Vector2f design_size = WorkingAsset()->DesignSize();
            const Vector2f preview_size = _designer_preview != nullptr ? _designer_preview->GetContentRect().zw : Vector2f::kZero;
            _asset_name->SetText(WorkingAsset()->Name());
            _design_size->SetText(std::format("Design Size: {:.0f} x {:.0f}", design_size.x, design_size.y));
            if (_design_width_input != nullptr && !_design_width_input->IsEditing())
                _design_width_input->SetContent(std::format("{:.0f}", design_size.x), false);
            if (_design_height_input != nullptr && !_design_height_input->IsEditing())
                _design_height_input->SetContent(std::format("{:.0f}", design_size.y), false);
            _preview_size->SetText(std::format("Preview Size: {:.0f} x {:.0f}", preview_size.x, preview_size.y));
            _designer_zoom_label->SetText(std::format("{}%", static_cast<i32>(std::round(_designer_zoom * 100.0f))));
            _designer_label->SetText(_is_preview_mode ? "Preview: click and scroll the widget  Esc: Design" :
                                                               "Wheel: Zoom  Right drag: Pan  Ctrl: Disable Snap");
            if (_designer_preview != nullptr) _designer_preview->InvalidatePaint();
        }

        void WidgetEditor::SetPreviewMode(bool is_preview)
        {
            if (is_preview && WorkingAsset() == nullptr)
                is_preview = false;
            if (_is_preview_mode == is_preview)
            {
                RefreshModeState();
                return;
            }

            if (!is_preview)
            {
                ClosePreviewPopup();
                if (UI::UIManager *ui_manager = UI::UIManager::Get(); ui_manager != nullptr)
                {
                    if (IsPreviewElement(ui_manager->GetFocusedElement()))
                        ui_manager->ClearFocus(ui_manager->GetFocusedElement());
                    if (IsPreviewElement(ui_manager->_capture_target))
                        ui_manager->_capture_target = nullptr;
                }
                _preview_capture_element = nullptr;
            }

            _is_preview_mode = is_preview;
            if (_preview_widget != nullptr)
                _preview_widget->_is_receive_event = false;
            RefreshModeState();
            RefreshDesigner();
        }

        void WidgetEditor::RefreshModeState()
        {
            if (_design_mode_button != nullptr)
                _design_mode_button->SetText(_is_preview_mode ? "Design" : "[Design]");
            if (_preview_mode_button != nullptr)
                _preview_mode_button->SetText(_is_preview_mode ? "[Preview]" : "Preview");
        }

        void WidgetEditor::ForwardPreviewEvent(const UI::UIEvent &event)
        {
            if (!_is_preview_mode || _preview_widget == nullptr || _preview_widget->Root() == nullptr)
                return;

            UI::UIManager *ui_manager = UI::UIManager::Get();
            if (ui_manager == nullptr)
                return;

            UI::UIEvent forwarded = event;
            if (event._type == UI::UIEvent::EType::kMouseExit)
            {
                forwarded._type = UI::UIEvent::EType::kMouseExitWindow;
                forwarded._mouse_position = {-1.0f, -1.0f};
            }
            else
            {
                forwarded._mouse_position = ScreenToDesign(event._mouse_position) +
                                             _preview_widget->Root()->GetArrangeRect().xy;
                forwarded._mouse_delta = _designer_zoom > 0.0f ? event._mouse_delta / _designer_zoom : event._mouse_delta;
            }
            forwarded._target = nullptr;
            forwarded._current_target = nullptr;
            forwarded._is_handled = false;

            if (event._type == UI::UIEvent::EType::kMouseMove || event._type == UI::UIEvent::EType::kMouseUp)
                ui_manager->_capture_target = _preview_capture_element;

            UI::Widget *popup_before = ui_manager->GetPopupWidget();
            _preview_widget->OnEvent(forwarded);
            if (event._type == UI::UIEvent::EType::kMouseDown)
                _preview_capture_element = ui_manager->_capture_target;
            if (event._type == UI::UIEvent::EType::kMouseUp)
                _preview_capture_element = nullptr;
            ui_manager->_capture_target = nullptr;

            UI::Widget *popup_after = ui_manager->GetPopupWidget();
            if (popup_after != popup_before && popup_after != nullptr)
            {
                _preview_popup_widget = popup_after;
                const Vector2f popup_design_position = popup_after->Root() == nullptr ? Vector2f::kZero :
                    popup_after->Root()->GetArrangeRect().xy;
                popup_after->SetPosition(DesignToScreen(popup_design_position));
            }
            else if (popup_after == nullptr)
            {
                _preview_popup_widget = nullptr;
            }
        }

        void WidgetEditor::ClosePreviewPopup()
        {
            UI::UIManager *ui_manager = UI::UIManager::Get();
            if (ui_manager != nullptr && _preview_popup_widget != nullptr &&
                ui_manager->GetPopupWidget() == _preview_popup_widget)
                ui_manager->HidePopup();
            _preview_popup_widget = nullptr;
        }

        bool WidgetEditor::IsPreviewElement(UI::UIElement *element) const
        {
            if (element == nullptr || _preview_widget == nullptr || _preview_widget->Root() == nullptr)
                return false;
            for (UI::UIElement *node = element; node != nullptr; node = node->GetParent())
            {
                if (node == _preview_widget->Root())
                    return true;
            }
            return false;
        }

        void WidgetEditor::RebuildPreview()
        {
            ClosePreviewPopup();
            if (_preview_widget != nullptr)
            {
                if (UI::UIManager *ui_manager = UI::UIManager::Get(); ui_manager != nullptr)
                {
                    if (IsPreviewElement(ui_manager->GetFocusedElement()))
                        ui_manager->ClearFocus(ui_manager->GetFocusedElement());
                    if (IsPreviewElement(ui_manager->_capture_target))
                        ui_manager->_capture_target = nullptr;
                }
            }
            _preview_capture_element = nullptr;
            if (_preview_widget != nullptr && UI::UIManager::Get() != nullptr)
                UI::UIManager::Get()->UnRegisterWidget(_preview_widget.get());
            _preview_widget.reset();
            if (WorkingAsset() == nullptr) return;

            const Vector2f design_size = WorkingAsset()->DesignSize();
            const u16 width = static_cast<u16>(std::clamp(design_size.x, 1.0f, 16384.0f));
            const u16 height = static_cast<u16>(std::clamp(design_size.y, 1.0f, 16384.0f));
            if (_preview_render_texture == nullptr || _preview_render_texture->Width() != width ||
                _preview_render_texture->Height() != height)
            {
                _preview_render_texture = Render::RenderTexture::Create(width, height, "WidgetDesignerPreview");
            }
            _preview_widget = WorkingAsset()->CreateInstance();
            if (_preview_widget == nullptr) return;
            _preview_widget->SetSize(design_size);
            _preview_widget->BindOutput(_preview_render_texture.get());
            _preview_widget->_is_receive_event = false;
            if (UI::UIManager::Get() != nullptr) UI::UIManager::Get()->RegisterWidget(_preview_widget);
            if (UI::UIElement *root = _preview_widget->Root(); root != nullptr)
                root->InvalidateLayout(true);
            _preview_widget->Update(0.0f);
        }

        void WidgetEditor::DestroyPreview()
        {
            ClosePreviewPopup();
            if (_preview_widget != nullptr)
            {
                if (UI::UIManager *ui_manager = UI::UIManager::Get(); ui_manager != nullptr)
                {
                    if (IsPreviewElement(ui_manager->GetFocusedElement()))
                        ui_manager->ClearFocus(ui_manager->GetFocusedElement());
                    if (IsPreviewElement(ui_manager->_capture_target))
                        ui_manager->_capture_target = nullptr;
                }
            }
            if (_preview_widget != nullptr && UI::UIManager::Get() != nullptr)
                UI::UIManager::Get()->UnRegisterWidget(_preview_widget.get());
            _preview_widget.reset();
            _preview_render_texture.reset();
            _preview_capture_element = nullptr;
        }

        void WidgetEditor::FitDesigner()
        {
            if (WorkingAsset() == nullptr || _designer_preview == nullptr) return;
            const Vector4f content_rect = _designer_preview->GetContentRect();
            const Vector2f design_size = WorkingAsset()->DesignSize();
            if (content_rect.z <= 0.0f || content_rect.w <= 0.0f || design_size.x <= 0.0f || design_size.y <= 0.0f)
                return;
            _designer_zoom = std::clamp(std::min((content_rect.z - 24.0f) / design_size.x,
                                                 (content_rect.w - 24.0f) / design_size.y), 0.05f, 8.0f);
            _designer_pan = Vector2f::kZero;
            _designer_needs_fit = false;
            RefreshDesigner();
        }

        UI::UIElement *WidgetEditor::ResolveAssetElement(UI::UIElement *preview_element) const
        {
            if (WorkingAsset() == nullptr || _preview_widget == nullptr || preview_element == nullptr) return nullptr;
            const Guid &guid = preview_element->GuidValue();
            if (!guid.IsEmpty())
            {
                if (UI::UIElement *element = WorkingRoot()->FindChildByGuid(guid); element != nullptr)
                    return element;
            }
            Vector<u32> child_indices;
            for (UI::UIElement *element = preview_element; element != _preview_widget->Root(); element = element->GetParent())
            {
                if (element == nullptr || element->GetParent() == nullptr) return nullptr;
                const i32 index = element->GetParent()->IndexOf(element);
                if (index < 0) return nullptr;
                child_indices.push_back(static_cast<u32>(index));
            }
            UI::UIElement *result = WorkingRoot();
            for (auto iter = child_indices.rbegin(); result != nullptr && iter != child_indices.rend(); ++iter)
                result = result->ChildAt(*iter);
            return result;
        }

        UI::UIElement *WidgetEditor::ResolvePreviewElement(UI::UIElement *asset_element) const
        {
            if (WorkingAsset() == nullptr || _preview_widget == nullptr || asset_element == nullptr) return nullptr;
            const Guid &guid = asset_element->GuidValue();
            if (!guid.IsEmpty())
            {
                if (UI::UIElement *element = _preview_widget->Root()->FindChildByGuid(guid); element != nullptr)
                    return element;
            }
            Vector<u32> child_indices;
            for (UI::UIElement *element = asset_element; element != WorkingRoot(); element = element->GetParent())
            {
                if (element == nullptr || element->GetParent() == nullptr) return nullptr;
                const i32 index = element->GetParent()->IndexOf(element);
                if (index < 0) return nullptr;
                child_indices.push_back(static_cast<u32>(index));
            }
            UI::UIElement *result = _preview_widget->Root();
            for (auto iter = child_indices.rbegin(); result != nullptr && iter != child_indices.rend(); ++iter)
                result = result->ChildAt(*iter);
            return result;
        }

        Vector2f WidgetEditor::ScreenToDesign(Vector2f screen_pos) const
        {
            if (_designer_preview == nullptr || WorkingAsset() == nullptr || _designer_zoom <= 0.0f) return Vector2f::kZero;
            const Vector4f content_rect = _designer_preview->GetContentRect();
            const Vector2f preview_size = WorkingAsset()->DesignSize() * _designer_zoom;
            const Vector2f preview_origin = content_rect.xy + content_rect.zw * 0.5f + _designer_pan - preview_size * 0.5f;
            return (screen_pos - preview_origin) / _designer_zoom;
        }

        Vector2f WidgetEditor::DesignToScreen(Vector2f design_pos) const
        {
            if (_designer_preview == nullptr || WorkingAsset() == nullptr) return Vector2f::kZero;
            const Vector4f content_rect = _designer_preview->GetContentRect();
            const Vector2f preview_size = WorkingAsset()->DesignSize() * _designer_zoom;
            const Vector2f preview_origin = content_rect.xy + content_rect.zw * 0.5f + _designer_pan - preview_size * 0.5f;
            return preview_origin + design_pos * _designer_zoom;
        }

        void WidgetEditor::RefreshDirtyState()
        {
            if (WorkingAsset() == nullptr)
            {
                SetTitle("Widget Editor");
                _status_text->SetText("No Widget Asset");
                return;
            }

            SetTitle("Widget Editor - " + _asset->Name() + (IsDirty() ? " *" : ""));
            _status_text->SetText(IsDirty() ? "Unsaved changes" : "Saved");
        }

        bool WidgetEditor::CanReparent(UI::UIElement *source, UI::UIElement *new_parent) const
        {
            if (WorkingAsset() == nullptr || source == nullptr || new_parent == nullptr || source == new_parent || source == WorkingRoot())
                return false;
            if (!CanAcceptChild(new_parent, source))
                return false;
            for (UI::UIElement *ancestor = new_parent; ancestor != nullptr; ancestor = ancestor->GetParent())
            {
                if (ancestor == source) return false;
            }
            return source->GetParent() != new_parent;
        }

        bool WidgetEditor::CanInsertElement(UI::UIElement *source, UI::UIElement *target, bool insert_before) const
        {
            if (source == nullptr || target == nullptr || source == target || source == WorkingRoot())
                return false;
            UI::UIElement *parent = target->GetParent();
            if (!CanAcceptChild(parent, source))
                return false;
            for (UI::UIElement *ancestor = parent; ancestor != nullptr; ancestor = ancestor->GetParent())
            {
                if (ancestor == source)
                    return false;
            }
            if (source->GetParent() != parent)
                return true;
            const i32 source_index = parent->IndexOf(source);
            const i32 target_index = parent->IndexOf(target);
            if (source_index < 0 || target_index < 0)
                return false;
            const i32 insertion_index = target_index + (insert_before ? 0 : 1);
            const i32 adjusted_index = source_index < insertion_index ? insertion_index - 1 : insertion_index;
            return adjusted_index != source_index;
        }

        bool WidgetEditor::CanAcceptChild(const UI::UIElement *parent, const UI::UIElement *child) const
        {
            if (parent == nullptr)
                return false;
            if (dynamic_cast<const UI::Canvas *>(parent) != nullptr || dynamic_cast<const UI::LinearBox *>(parent) != nullptr)
                return true;
            const auto *border = dynamic_cast<const UI::Border *>(parent);
            return border != nullptr && border->GetChildren().empty();
        }

        void WidgetEditor::OnHierarchyDrop(UI::TreeItemId source_item, UI::TreeItemId target_item)
        {
            ReparentElement(_tree_data_source->ToElement(source_item), _tree_data_source->ToElement(target_item));
        }

        void WidgetEditor::ReparentElement(UI::UIElement *source, UI::UIElement *new_parent)
        {
            if (!CanReparent(source, new_parent)) return;
            Ref<UI::UIElement> before_root = CloneWorkingRoot();
            const Guid before_selection = _selected_guid;
            Ref<UI::UIElement> source_ref;
            for (const Ref<UI::UIElement> &child : source->GetParent()->GetChildren())
            {
                if (child.get() == source)
                {
                    source_ref = child;
                    break;
                }
            }
            if (source_ref == nullptr) return;
            new_parent->AddChild(std::move(source_ref));
            SetSelectedElement(source);
            CommitTreeEdit(std::move(before_root), before_selection);
        }

        void WidgetEditor::InsertElement(UI::UIElement *source, UI::UIElement *target, bool insert_before)
        {
            if (!CanInsertElement(source, target, insert_before))
                return;
            UI::UIElement *target_parent = target->GetParent();
            Ref<UI::UIElement> before_root = CloneWorkingRoot();
            const Guid before_selection = _selected_guid;
            const i32 target_index = target_parent->IndexOf(target);
            if (target_index < 0)
                return;
            if (source->GetParent() == target_parent)
            {
                const i32 source_index = target_parent->IndexOf(source);
                i32 insertion_index = target_index + (insert_before ? 0 : 1);
                if (source_index < insertion_index)
                    --insertion_index;
                if (!target_parent->MoveChild(source, static_cast<u32>(insertion_index)))
                    return;
            }
            else
            {
                Ref<UI::UIElement> source_ref;
                for (const Ref<UI::UIElement> &child : source->GetParent()->GetChildren())
                {
                    if (child.get() == source)
                    {
                        source_ref = child;
                        break;
                    }
                }
                if (source_ref == nullptr)
                    return;
                if (target_parent->AddChild(std::move(source_ref)) == nullptr)
                    return;
                target_parent->MoveChild(source, static_cast<u32>(target_index + (insert_before ? 0 : 1)));
            }
            SetSelectedElement(source);
            CommitTreeEdit(std::move(before_root), before_selection);
        }

        void WidgetEditor::DeleteSelectedElement()
        {
            if (_selected_element == nullptr || WorkingAsset() == nullptr || _selected_element == WorkingRoot() ||
                _selected_element->GetParent() == nullptr)
                return;
            Ref<UI::UIElement> before_root = CloneWorkingRoot();
            const Guid before_selection = _selected_guid;
            UI::UIElement *parent = _selected_element->GetParent();
            parent->RemoveChild(_selected_element);
            SetSelectedElement(parent);
            CommitTreeEdit(std::move(before_root), before_selection);
        }

        void WidgetEditor::RenameSelectedElement(Vector2f popup_pos)
        {
            if (_selected_element == nullptr) return;
            UI::UIElement *element = _selected_element;
            EditorPopup::ShowTextInputAt(popup_pos, "Rename Widget Element", element->Name(), [this, element](const String &value)
            {
                const String name = TrimName(value);
                if (name.empty()) return std::optional<String>("Name cannot be empty.");
                Ref<UI::UIElement> before_root = CloneWorkingRoot();
                const Guid before_selection = _selected_guid;
                element->Name(name);
                CommitTreeEdit(std::move(before_root), before_selection);
                return std::optional<String>{};
            });
        }

        void WidgetEditor::ReorderSelectedElement(i32 direction)
        {
            if (_selected_element == nullptr || _selected_element->GetParent() == nullptr) return;
            UI::UIElement *parent = _selected_element->GetParent();
            const i32 old_index = parent->IndexOf(_selected_element);
            const i32 new_index = old_index + direction;
            if (old_index < 0 || new_index < 0 || new_index >= static_cast<i32>(parent->GetChildren().size())) return;
            Ref<UI::UIElement> before_root = CloneWorkingRoot();
            const Guid before_selection = _selected_guid;
            if (!parent->MoveChild(_selected_element, static_cast<u32>(new_index))) return;
            CommitTreeEdit(std::move(before_root), before_selection);
        }

        void WidgetEditor::DuplicateSelectedElement()
        {
            if (_selected_element == nullptr || _selected_element == WorkingRoot() || _selected_element->GetParent() == nullptr)
                return;
            UI::UIElement *parent = _selected_element->GetParent();
            if (!CanAcceptChild(parent, _selected_element))
                return;
            Ref<UI::UIElement> before_root = CloneWorkingRoot();
            const Guid before_selection = _selected_guid;
            Ref<UI::UIElement> duplicate = CloneUIElementTree(Ref<UI::UIElement>(_selected_element,
                [](UI::UIElement *) {}));
            if (duplicate == nullptr)
                return;
            duplicate->RegenerateGuidRecursive();
            UI::CanvasSlot *source_slot = dynamic_cast<UI::CanvasSlot *>(_selected_element->GetSlot().get());
            UI::UIElement *duplicate_element = parent->AddChild(std::move(duplicate));
            if (duplicate_element == nullptr)
                return;
            if (source_slot != nullptr)
            {
                if (auto *duplicate_slot = dynamic_cast<UI::CanvasSlot *>(duplicate_element->GetSlot().get()); duplicate_slot != nullptr)
                    duplicate_slot->Position(source_slot->_position + Vector2f(10.0f, 10.0f)).Size(source_slot->_size);
            }
            const i32 source_index = parent->IndexOf(_selected_element);
            if (source_index >= 0)
                parent->MoveChild(duplicate_element, static_cast<u32>(source_index + 1));
            SetSelectedElement(duplicate_element);
            CommitTreeEdit(std::move(before_root), before_selection);
        }

        void WidgetEditor::ShowHierarchyContextMenu(UI::TreeItemId item, Vector2f popup_pos)
        {
            SetSelectedElement(_tree_data_source->ToElement(item));
            if (_selected_element == nullptr) return;
            Vector<PopupMenuAction> actions;
            actions.push_back({"Rename", [this, popup_pos]() { RenameSelectedElement(popup_pos); }});
            if (_selected_element != WorkingRoot())
            {
                actions.push_back({"Duplicate", [this]() { DuplicateSelectedElement(); }});
                actions.push_back({"Move Up", [this]() { ReorderSelectedElement(-1); }});
                actions.push_back({"Move Down", [this]() { ReorderSelectedElement(1); }});
                actions.push_back({"Reparent to Root", [this]() { ReparentElement(_selected_element, WorkingRoot()); }});
                actions.push_back({"Delete", [this]() { DeleteSelectedElement(); }, true});
            }
            EditorPopup::ShowActionMenuAt(popup_pos, actions);
        }
    }// namespace Editor
}// namespace Ailu
