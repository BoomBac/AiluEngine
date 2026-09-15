#include "Widgets/PlaceActor.h"
#include "Assets/Asset.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "PlaceActor/PlaceActorRegistry.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/DragDrop.h"

#include <algorithm>
#include <utility>

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kDragThreshold = 5.0f;
            constexpr f32 kEntryHeight = 30.0f;

            struct PrimitiveDefinition
            {
                const char *_id;
                const char *_display_name;
                const wchar_t *_asset_path;
                i32 _order;
            };

            constexpr PrimitiveDefinition kPrimitiveDefinitions[] = {
                    {"basic.cube", "Cube", L"Meshs/src_res/cube.alasset", 0},
                    {"basic.sphere", "Sphere", L"Meshs/src_res/sphere.alasset", 1},
                    {"basic.capsule", "Capsule", L"Meshs/src_res/capsule.alasset", 2},
                    {"basic.cylinder", "Cylinder", L"Meshs/src_res/cylinder.alasset", 3},
                    {"basic.cone", "Cone", L"Meshs/src_res/cone.alasset", 4},
                    {"basic.torus", "Torus", L"Meshs/src_res/torus.alasset", 5},
                    {"basic.plane", "Plane", L"Meshs/src_res/plane.alasset", 6},
                    {"basic.monkey", "Monkey", L"Meshs/src_res/monkey.alasset", 7}};
        }

        PlaceActor::PlaceActor() : DockWindow("Place Actor", {440.0f, 520.0f})
        {
            BuildUI();
        }

        void PlaceActor::BuildUI()
        {
            _split_view = _content_root->AddChild<UI::SplitView>();
            _split_view->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            _split_view->_is_horizontal = true;
            _split_view->SetRatio(0.28f);

            _category_box = _split_view->AddChild<UI::VerticalBox>();
            _category_box->SlotPadding() = UI::Padding(5.0f);
            _entry_scroll = _split_view->AddChild<UI::ScrollView>();
            _entry_content = _entry_scroll->AddChild<UI::VerticalBox>();
            _entry_content->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            _entry_content->SlotPadding() = UI::Padding(6.0f);

            BuildEntries();
            BuildCategories();
            _category_buttons.reserve(_categories.size());
            for (u32 category_index = 0u; category_index < _categories.size(); ++category_index)
            {
                auto *button = _category_box->AddChild<UI::Button>(_categories[category_index]);
                button->GetSlotAs<UI::LinearSlot>()
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size({0.0f, 30.0f})
                        .Margin(UI::Padding(0.0f, 1.0f, 0.0f, 1.0f));
                button->OnMouseClick() += [this, category_index](UI::UIEvent &e)
                {
                    _selected_category = category_index;
                    RefreshEntries();
                    e._is_handled = true;
                };
                _category_buttons.emplace_back(button);
            }
            ResolveEntries();
            RefreshEntries();
        }

        void PlaceActor::BuildEntries()
        {
            constexpr size_t kPrimitiveCount = sizeof(kPrimitiveDefinitions) / sizeof(kPrimitiveDefinitions[0]);
            const auto &place_actor_entries = PlaceActorRegistry::Get().Entries();
            _entries.reserve(kPrimitiveCount + place_actor_entries.size());
            for (const auto &definition : kPrimitiveDefinitions)
            {
                PlaceActorUIEntry entry;
                entry._id = definition._id;
                entry._display_name = definition._display_name;
                entry._category = "Basic Geometry";
                entry._order = definition._order;
                entry._asset_path = definition._asset_path;
                entry._is_mesh = true;
                _entries.emplace_back(std::move(entry));
            }
            for (const auto &desc : place_actor_entries)
            {
                PlaceActorUIEntry entry;
                entry._id = desc._id;
                entry._display_name = desc._display_name;
                entry._category = desc._category;
                entry._order = desc._order;
                _entries.emplace_back(std::move(entry));
            }

            std::sort(_entries.begin(), _entries.end(), [](const PlaceActorUIEntry &left, const PlaceActorUIEntry &right)
            {
                if (left._category != right._category)
                    return left._category < right._category;
                if (left._order != right._order)
                    return left._order < right._order;
                return left._display_name < right._display_name;
            });
        }

        void PlaceActor::BuildCategories()
        {
            _categories.clear();
            for (const auto &entry : _entries)
            {
                if (std::find(_categories.begin(), _categories.end(), entry._category) == _categories.end())
                    _categories.emplace_back(entry._category);
            }
        }

        void PlaceActor::RefreshEntries()
        {
            if (_entry_content == nullptr)
                return;
            _entry_content->ClearChildren();

            auto *hint = _entry_content->AddChild<UI::Text>("Drag an actor into the Scene View");
            hint->_color = Color(0.65f, 0.70f, 0.78f, 1.0f);
            hint->FontSize(12.0f);
            hint->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f})
                    .Margin(UI::Padding(2.0f, 0.0f, 2.0f, 6.0f));

            if (_selected_category >= _categories.size())
                _selected_category = 0u;
            if (_categories.empty())
                return;
            auto *category = _entry_content->AddChild<UI::Text>(_categories[_selected_category]);
            category->_color = Color(0.82f, 0.86f, 0.92f, 1.0f);
            category->FontSize(13.0f);
            category->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f});

            bool has_entries = false;
            for (u32 i = 0u; i < _entries.size(); ++i)
            {
                if (_entries[i]._category != _categories[_selected_category])
                    continue;
                has_entries = true;
                const i32 entry_index = static_cast<i32>(i);
                auto *button = _entry_content->AddChild<UI::Button>(_entries[i]._display_name);
                button->GetSlotAs<UI::LinearSlot>()
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size({0.0f, kEntryHeight})
                        .Margin(UI::Padding(0.0f, 1.0f, 0.0f, 1.0f));
                button->OnMouseDown() += [this, entry_index](UI::UIEvent &e)
                {
                    if (e._key_code != EKey::kLBUTTON)
                        return;
                    _drag_source_index = entry_index;
                    _drag_start_pos = e._mouse_position;
                    _is_dragging = false;
                };
                button->OnMouseMove() += [this, entry_index](UI::UIEvent &e)
                {
                    if (_drag_source_index != entry_index || _is_dragging || !Input::IsKeyDown(EKey::kLBUTTON))
                        return;
                    if (Magnitude(e._mouse_position - _drag_start_pos) <= kDragThreshold)
                        return;
                    _is_dragging = true;
                    BeginActorDrag(entry_index);
                };
                button->OnMouseUp() += [this, entry_index](UI::UIEvent &)
                {
                    if (_drag_source_index == entry_index)
                    {
                        _drag_source_index = -1;
                        _is_dragging = false;
                    }
                };
            }
            if (!has_entries)
            {
                auto *empty = _entry_content->AddChild<UI::Text>("No presets in this category yet.");
                empty->_color = Color(0.55f, 0.60f, 0.68f, 1.0f);
                empty->FontSize(12.0f);
                empty->GetSlotAs<UI::LinearSlot>()
                        .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                        .Size({0.0f, 24.0f});
            }
        }

        void PlaceActor::ResolveEntries()
        {
            for (auto &entry : _entries)
            {
                if (!entry._is_mesh)
                    continue;
                if (entry._mesh == nullptr)
                {
                    entry._mesh = ResourceMgr::Get().GetRef<Render::Mesh>(entry._asset_path);
                    if (entry._mesh == nullptr)
                        entry._mesh = ResourceMgr::Get().Load<Render::Mesh>(entry._asset_path);
                }
                if (entry._asset == nullptr)
                    entry._asset = ResourceMgr::Get().GetAsset(entry._asset_path);
            }
        }

        void PlaceActor::BeginActorDrag(i32 entry_index)
        {
            if (entry_index < 0 || static_cast<size_t>(entry_index) >= _entries.size())
                return;
            auto &entry = _entries[entry_index];
            if (entry._is_mesh)
            {
                ResolveEntries();
                if (entry._asset == nullptr)
                {
                    LOG_WARNING(L"PlaceActor: primitive asset is not available: {}",
                                entry._asset_path);
                    return;
                }
                UI::DragDropManager::Get().BeginDrag({UI::EDragType::kMesh, entry._asset}, entry._display_name);
                return;
            }
            _drag_payload._id = entry._id;
            UI::DragDropManager::Get().BeginDrag({UI::EDragType::kCustomUserData, &_drag_payload},
                                                 entry._display_name);
        }

        void PlaceActor::Update(f32 dt)
        {
            DockWindow::Update(dt);
            ResolveEntries();
            if (!Input::IsKeyDown(EKey::kLBUTTON))
            {
                _drag_source_index = -1;
                _is_dragging = false;
            }
        }
    }// namespace Editor
}// namespace Ailu
