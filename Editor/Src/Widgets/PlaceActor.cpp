#include "Widgets/PlaceActor.h"
#include "Assets/Asset.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Scene/Scene.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/DragDrop.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            constexpr f32 kDragThreshold = 5.0f;
            constexpr f32 kEntryHeight = 30.0f;
            constexpr u32 kBasicGeometryCategory = 0u;

            struct PrimitiveDefinition
            {
                const char *_name;
                const wchar_t *_asset_path;
                u32 _category;
                EPlaceActorPresetType _type;
            };

            constexpr const char *kCategoryNames[] = {"Basic Geometry", "Lighting", "Rendering"};
            constexpr PrimitiveDefinition kPrimitiveDefinitions[] = {
                    {"Cube", L"Meshs/src_res/cube.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Sphere", L"Meshs/src_res/sphere.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Capsule", L"Meshs/src_res/capsule.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Cylinder", L"Meshs/src_res/cylinder.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Cone", L"Meshs/src_res/cone.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Torus", L"Meshs/src_res/torus.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Plane", L"Meshs/src_res/plane.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Monkey", L"Meshs/src_res/monkey.alasset", kBasicGeometryCategory, EPlaceActorPresetType::kMesh},
                    {"Directional Light", nullptr, 1u, EPlaceActorPresetType::kDirectionalLight},
                    {"Point Light", nullptr, 1u, EPlaceActorPresetType::kPointLight},
                    {"Spot Light", nullptr, 1u, EPlaceActorPresetType::kSpotLight},
                    {"Area Light", nullptr, 1u, EPlaceActorPresetType::kAreaLight},
                    {"Light Probe", nullptr, 2u, EPlaceActorPresetType::kLightProbe}};
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

            constexpr size_t kCategoryCount = sizeof(kCategoryNames) / sizeof(kCategoryNames[0]);
            _category_buttons.reserve(kCategoryCount);
            for (u32 category_index = 0u; category_index < kCategoryCount; ++category_index)
            {
                auto *button = _category_box->AddChild<UI::Button>(kCategoryNames[category_index]);
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

            constexpr size_t kPrimitiveCount = sizeof(kPrimitiveDefinitions) / sizeof(kPrimitiveDefinitions[0]);
            _entry_names.reserve(kPrimitiveCount);
            _entry_paths.reserve(kPrimitiveCount);
            _entry_categories.reserve(kPrimitiveCount);
            _entry_types.reserve(kPrimitiveCount);
            _entry_assets.resize(kPrimitiveCount);
            _entry_meshes.resize(kPrimitiveCount);
            _entry_buttons.resize(kPrimitiveCount, nullptr);
            for (const auto &definition : kPrimitiveDefinitions)
            {
                _entry_names.emplace_back(definition._name);
                _entry_paths.emplace_back(definition._asset_path != nullptr ? definition._asset_path : L"");
                _entry_categories.emplace_back(definition._category);
                _entry_types.emplace_back(definition._type);
            }
            ResolveEntries();
            RefreshEntries();
        }

        void PlaceActor::RefreshEntries()
        {
            if (_entry_content == nullptr)
                return;
            _entry_content->ClearChildren();
            std::fill(_entry_buttons.begin(), _entry_buttons.end(), nullptr);

            auto *hint = _entry_content->AddChild<UI::Text>("Drag an actor into the Scene View");
            hint->_color = Color(0.65f, 0.70f, 0.78f, 1.0f);
            hint->FontSize(12.0f);
            hint->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f})
                    .Margin(UI::Padding(2.0f, 0.0f, 2.0f, 6.0f));

            auto *category = _entry_content->AddChild<UI::Text>(kCategoryNames[_selected_category]);
            category->_color = Color(0.82f, 0.86f, 0.92f, 1.0f);
            category->FontSize(13.0f);
            category->GetSlotAs<UI::LinearSlot>()
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
                    .Size({0.0f, 24.0f});

            bool has_entries = false;
            for (u32 i = 0u; i < _entry_names.size(); ++i)
            {
                if (_entry_categories[i] != _selected_category)
                    continue;
                has_entries = true;
                const i32 entry_index = static_cast<i32>(i);
                auto *button = _entry_content->AddChild<UI::Button>(_entry_names[i]);
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
                _entry_buttons[i] = button;
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
            for (u32 i = 0u; i < _entry_names.size(); ++i)
            {
                if (_entry_types[i] != EPlaceActorPresetType::kMesh)
                    continue;
                if (_entry_meshes[i] == nullptr)
                {
                    _entry_meshes[i] = ResourceMgr::Get().GetRef<Render::Mesh>(_entry_paths[i]);
                    if (_entry_meshes[i] == nullptr)
                        _entry_meshes[i] = ResourceMgr::Get().Load<Render::Mesh>(_entry_paths[i]);
                }
                if (_entry_assets[i] == nullptr)
                    _entry_assets[i] = ResourceMgr::Get().GetAsset(_entry_paths[i]);
            }
        }

        void PlaceActor::BeginActorDrag(i32 entry_index)
        {
            if (entry_index < 0 || static_cast<size_t>(entry_index) >= _entry_names.size())
                return;
            if (_entry_types[entry_index] == EPlaceActorPresetType::kMesh)
            {
                ResolveEntries();
                if (_entry_assets[entry_index] == nullptr)
                {
                    LOG_WARNING(L"PlaceActor: primitive asset is not available: {}",
                                _entry_paths[entry_index]);
                    return;
                }
                UI::DragDropManager::Get().BeginDrag({UI::EDragType::kMesh, _entry_assets[entry_index]},
                                                     _entry_names[entry_index]);
                return;
            }
            _drag_payload._type = _entry_types[entry_index];
            _drag_payload._display_name = _entry_names[entry_index].c_str();
            UI::DragDropManager::Get().BeginDrag({UI::EDragType::kCustomUserData, &_drag_payload},
                                                 _entry_names[entry_index]);
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
