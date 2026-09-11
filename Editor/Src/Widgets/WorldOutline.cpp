#include "Widgets/WorldOutline.h"
#include "Common/Selection.h"
#include "Common/Undo.h"
#include "Common/EditorPopup.h"
#include "Common/CameraControllers.h"
#include "EditorApp.h"
#include "Assets/PrefabAsset.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Events/Event.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"
#include "UI/TreeView.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"
#include "Ext/imgui/imgui.h"

namespace Ailu
{
    namespace Editor
    {
        using namespace UI;
        using SceneManagement::SceneMgr;
        using SceneManagement::Scene;

        // =========================================================================
        // SceneTreeDataSource
        // =========================================================================
        class WorldOutline::SceneTreeDataSource final : public ITreeViewDataSource
        {
        public:
            void SetScene(Scene* scene) { _scene = scene; }

            static TreeItemId ToTreeItem(ECS::Entity e)
            {
                return static_cast<TreeItemId>(e);
            }
            static ECS::Entity ToEntity(TreeItemId id)
            {
                return static_cast<ECS::Entity>(id);
            }

            Vector<TreeItemId> GetRootItems() const override
            {
                Vector<TreeItemId> roots;
                if (!_scene) return roots;
                auto& reg = _scene->GetRegister();
                for (auto e : _scene->EntityView())
                {
                    if (!reg.IsAlive(e)) continue;
                    auto* hier = reg.GetComponent<ECS::CHierarchy>(e);
                    if (hier && hier->_parent == ECS::kInvalidEntity)
                        roots.push_back(ToTreeItem(e));
                }
                return roots;
            }

            Vector<TreeItemId> GetChildren(TreeItemId parent) const override
            {
                Vector<TreeItemId> children;
                auto entity = ToEntity(parent);
                if (!_scene || !_scene->IsValidEntity(entity)) return children;

                auto& reg = _scene->GetRegister();
                auto* hier = reg.GetComponent<ECS::CHierarchy>(entity);
                if (!hier || hier->_children_num == 0) return children;

                std::unordered_set<ECS::Entity> visited;
                u32 count = 0;
                ECS::Entity child = hier->_first_child;
                while (child != ECS::kInvalidEntity && count < 1000)
                {
                    if (!visited.insert(child).second)
                    {
                        LOG_WARNING("SceneTreeDataSource: cycle detected at entity {}", child);
                        break;
                    }
                    if (reg.IsAlive(child) && reg.HasComponent<ECS::CHierarchy>(child))
                        children.push_back(ToTreeItem(child));
                    auto* child_hier = reg.GetComponent<ECS::CHierarchy>(child);
                    ECS::Entity next = (child_hier ? child_hier->_next_sibling : ECS::kInvalidEntity);
                    child = next;
                    ++count;
                }
                return children;
            }

            TreeItemId GetParent(TreeItemId item) const override
            {
                auto entity = ToEntity(item);
                if (!_scene || !_scene->IsValidEntity(entity)) return kInvalidTreeItemId;

                auto* hier = _scene->GetRegister().GetComponent<ECS::CHierarchy>(entity);
                if (!hier || hier->_parent == ECS::kInvalidEntity) return kInvalidTreeItemId;
                if (!_scene->IsValidEntity(hier->_parent))
                {
                    LOG_WARNING("SceneTreeDataSource: parent {} of {} is not alive", hier->_parent, entity);
                    return kInvalidTreeItemId;
                }
                return ToTreeItem(hier->_parent);
            }

            TreeItemPresentation GetPresentation(TreeItemId item) const override
            {
                TreeItemPresentation result;
                auto entity = ToEntity(item);
                if (_scene && _scene->IsValidEntity(entity))
                {
                    auto* tag = _scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
                    result._label = tag && !tag->_name.empty() ? tag->_name : "<Unnamed>";
                    result._draggable = true;
                    result._drop_target = true;
                    if (!_scene->IsEntityEnabled(entity))
                        result._text_color = Color(0.42f, 0.45f, 0.52f, 1.0f);
                    if (tag != nullptr && !tag->_prefab_entity.IsEmpty())
                        result._text_color = Color(0.35f, 0.65f, 1.0f, 1.0f);
                }
                else
                {
                    result._label = "<Invalid>";
                }
                result._selectable = true;
                result._draggable = true;
                result._drop_target = true;
                return result;
            }

            bool IsValid(TreeItemId item) const override
            {
                auto entity = ToEntity(item);
                return _scene && _scene->IsValidEntity(entity);
            }

        private:
            Scene* _scene = nullptr;
        };

        // =========================================================================
        // WorldOutline
        // =========================================================================
        WorldOutline::WorldOutline() : DockWindow("WorldOutline")
        {
            _data_source = MakeScope<SceneTreeDataSource>();
            BuildUI();
            BindTreeEvents();

            // Initial scene
            auto* scene = SceneMgr::Get().ActiveScene();
            if (scene)
            {
                _observed_scene = scene;
                _data_source->SetScene(scene);
                _tree_view->SetDataSource(_data_source.get());
                _observed_structure_revision = scene->StructureRevision();
                _observed_edit_revision = scene->EditRevision();
                UpdateSceneTitle(scene);
            }
            else
            {
                UpdateSceneTitle(nullptr);
            }
        }

        WorldOutline::~WorldOutline()
        {
            if (_tree_view)
                _tree_view->SetDataSource(nullptr);
        }

        void WorldOutline::BuildUI()
        {
            auto* vb = _content_root->AddChild<VerticalBox>();
            vb->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);

            // Toolbar
            auto* toolbar = vb->AddChild<HorizontalBox>();
            toolbar->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFixed).Size(Vector2f(0.0f, 28.0f));

            _back_button = toolbar->AddChild<Button>("<");
            _back_button->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(24.0f, 22.0f))
                    .Margin(Padding(2.0f, 0.0f, 0.0f, 0.0f));
            _back_button->OnMouseClick() += [this](UIEvent& e)
            {
                ExitTemporaryPrefabScene();
                e._is_handled = true;
            };
            _back_button->SetVisible(false);

            _scene_title = toolbar->AddChild<Text>("No Scene");
            _scene_title->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto);
            _scene_title->_color = Colors::kGray;
            _scene_title->GetSlotAs<LinearSlot>().Margin(Padding(4.0f, 0.0f, 0.0f, 0.0f));

            _add_button = toolbar->AddChild<Button>("+");
            _add_button->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFixed, ESizePolicy::kFixed).Size(Vector2f(24.0f, 22.0f))
                    .Margin(Padding(2.0f, 0.0f, 4.0f, 0.0f));
            _add_button->OnMouseClick() += [this](UIEvent& e)
            {
                auto* scene = SceneMgr::Get().ActiveScene();
                if (scene)
                {
                    auto new_entity = scene->AddObject("GameObject");
                    Selection::SetSelection(new_entity);
                }
            };

            // TreeView
            _tree_view = vb->AddChild<TreeView>();
            _tree_view->Name("WorldOutlineTree");
            _tree_view->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
            _tree_view->SetMultiSelectEnabled(true);
        }

        void WorldOutline::BindTreeEvents()
        {
            _tree_view->_on_selection_changed += [this](TreeItemId)
            {
                auto* scene = SceneMgr::Get().ActiveScene();
                if (!scene)
                    return;

                List<ECS::Entity> selected_entities;
                for (TreeItemId selected_item : _tree_view->GetSelectedItems())
                {
                    auto selected_entity = SceneTreeDataSource::ToEntity(selected_item);
                    if (scene->IsValidEntity(selected_entity))
                        selected_entities.push_back(selected_entity);
                }

                const List<ECS::Entity> previous_entities = Selection::SelectedEntities();
                if (previous_entities == selected_entities)
                    return;
                for (ECS::Entity previous_entity : previous_entities)
                    Selection::RemoveSlection(previous_entity);
                for (ECS::Entity selected_entity : selected_entities)
                    Selection::AddSelection(selected_entity);
            };

            _tree_view->_on_item_double_clicked += [this](TreeItemId item)
            {
                FocusCameraOnEntity(SceneTreeDataSource::ToEntity(item));
            };

            _tree_view->OnKeyDown() += [this](UIEvent &e)
            {
                if (e._key_code != EKey::kF2)
                    return;

                auto item = _tree_view->GetSelectedItem();
                auto *scene = SceneMgr::Get().ActiveScene();
                auto entity = SceneTreeDataSource::ToEntity(item);
                if (scene != nullptr && scene->IsValidEntity(entity))
                    BeginEntityRename(entity);
                e._is_handled = true;
            };

            _tree_view->_on_item_context_menu += [this](TreeItemId item, Vector2f pos)
            {
                auto entity = SceneTreeDataSource::ToEntity(item);
                auto* scene = SceneMgr::Get().ActiveScene();
                if (scene && scene->IsValidEntity(entity))
                    ShowEntityContextMenu(entity, pos);
            };

            // Drag callbacks
            _tree_view->SetCanDragCallback([](TreeItemId item) -> bool
            {
                auto* scene = SceneMgr::Get().ActiveScene();
                return scene && scene->IsValidEntity(SceneTreeDataSource::ToEntity(item));
            });

            _tree_view->SetCanDropCallback([](TreeView*, TreeItemId src, TreeItemId tgt) -> bool
            {
                if (src == tgt) return false;
                auto* scene = SceneMgr::Get().ActiveScene();
                if (!scene) return false;
                auto src_entity = SceneTreeDataSource::ToEntity(src);
                if (!scene->IsValidEntity(src_entity)) return false;
                if (tgt != kInvalidTreeItemId)
                {
                    auto tgt_entity = SceneTreeDataSource::ToEntity(tgt);
                    if (!scene->IsValidEntity(tgt_entity)) return false;
                    if (scene->IsDescendantOf(tgt_entity, src_entity)) return false;
                }
                return true;
            });

            _tree_view->SetDropCallback([this](TreeView*, TreeItemId src, TreeItemId tgt)
            {
                OnDropAction(nullptr, src, tgt);
            });
        }

        void WorldOutline::Update(f32 dt)
        {
            DockWindow::Update(dt);
            if (IsFocus() && Input::IsKeyJustPressed(EKey::kF2))
            {
                auto item = _tree_view->GetSelectedItem();
                auto *scene = SceneMgr::Get().ActiveScene();
                auto entity = SceneTreeDataSource::ToEntity(item);
                if (scene != nullptr && scene->IsValidEntity(entity))
                    BeginEntityRename(entity);
            }
            auto* scene = SceneMgr::Get().ActiveScene();

            if (scene != _observed_scene)
            {
                _observed_scene = scene;
                _data_source->SetScene(scene);
                _tree_view->SetDataSource(scene ? _data_source.get() : nullptr);
                _observed_structure_revision = scene ? scene->StructureRevision() : 0;
                _observed_edit_revision = scene ? scene->EditRevision() : 0;
                if (scene)
                {
                    UpdateSceneTitle(scene);
                    SyncSelectionFromEngine();
                }
                else
                {
                    UpdateSceneTitle(nullptr);
                }
            }
            else if (scene)
            {
                u64 rev = scene->StructureRevision();
                u64 edit_rev = scene->EditRevision();
                if (rev != _observed_structure_revision || edit_rev != _observed_edit_revision)
                {
                    _observed_structure_revision = rev;
                    _observed_edit_revision = edit_rev;
                    _tree_view->Refresh();
                }
            }
            UpdateSceneTitle(scene);

            u64 sel_rev = Selection::Revision();
            if (sel_rev != _observed_selection_revision)
            {
                _observed_selection_revision = sel_rev;
                SyncSelectionFromEngine();
            }
        }

        void WorldOutline::SyncSelectionFromEngine()
        {
            auto* scene = SceneMgr::Get().ActiveScene();
            if (!scene)
            {
                _tree_view->ClearSelection(false);
            }
            else
            {
                Vector<TreeItemId> selected_items;
                for (ECS::Entity entity : Selection::SelectedEntities())
                {
                    if (!scene->IsValidEntity(entity))
                        continue;
                    const TreeItemId item = SceneTreeDataSource::ToTreeItem(entity);
                    selected_items.push_back(item);
                    _tree_view->ExpandParents(item);
                }
                _tree_view->SetSelectedItems(selected_items, false);
                if (!selected_items.empty())
                    _tree_view->ScrollItemIntoView(selected_items.front());
            }
        }

        void WorldOutline::ShowEntityContextMenu(ECS::Entity entity, Vector2f position)
        {
            auto* scene = SceneMgr::Get().ActiveScene();
            if (!scene) return;

            auto& reg = scene->GetRegister();
            auto* tag = reg.GetComponent<ECS::TagComponent>(entity);
            String entity_name = tag ? tag->_name : "Entity";
            bool has_parent = false;
            if (auto* hier = reg.GetComponent<ECS::CHierarchy>(entity))
                has_parent = (hier->_parent != ECS::kInvalidEntity);

            Vector<ECS::Entity> selected_entities;
            for (TreeItemId selected_item : _tree_view->GetSelectedItems())
            {
                const ECS::Entity selected_entity = SceneTreeDataSource::ToEntity(selected_item);
                if (scene->IsValidEntity(selected_entity))
                    selected_entities.push_back(selected_entity);
            }
            bool context_entity_selected = false;
            for (ECS::Entity selected_entity : selected_entities)
            {
                if (selected_entity == entity)
                {
                    context_entity_selected = true;
                    break;
                }
            }
            if (!context_entity_selected)
                selected_entities.push_back(entity);

            Vector<PopupMenuAction> actions;

            actions.push_back({"Rename", [this, entity]() { BeginEntityRename(entity); }});

            actions.push_back({"Duplicate", [scene, selected_entities]()
            {
                Vector<ECS::Entity> duplicate_roots;
                for (ECS::Entity selected_entity : selected_entities)
                {
                    if (!scene->IsValidEntity(selected_entity))
                        continue;

                    bool has_selected_ancestor = false;
                    for (ECS::Entity potential_ancestor : selected_entities)
                    {
                        if (potential_ancestor != selected_entity &&
                            scene->IsDescendantOf(selected_entity, potential_ancestor))
                        {
                            has_selected_ancestor = true;
                            break;
                        }
                    }
                    if (!has_selected_ancestor)
                        duplicate_roots.push_back(selected_entity);
                }

                Vector<ECS::Entity> duplicated_entities;
                for (ECS::Entity duplicate_root : duplicate_roots)
                {
                    const ECS::Entity duplicated_entity = scene->DuplicateEntity(duplicate_root);
                    if (duplicated_entity != ECS::kInvalidEntity)
                        duplicated_entities.push_back(duplicated_entity);
                }

                for (ECS::Entity selected_entity : selected_entities)
                    Selection::RemoveSlection(selected_entity);
                for (ECS::Entity duplicated_entity : duplicated_entities)
                    Selection::AddSelection(duplicated_entity);
            }});

            actions.push_back({selected_entities.size() > 1u ? "Copy Entity GUIDs" : "Copy Entity GUID",
                               [scene, selected_entities]()
            {
                String guids;
                for (ECS::Entity selected_entity : selected_entities)
                {
                    if (!scene->IsValidEntity(selected_entity))
                        continue;
                    if (!guids.empty())
                        guids.push_back('\n');
                    guids += scene->GetEntityGuid(selected_entity).ToString();
                }
                ImGui::SetClipboardText(guids.c_str());
            }});

            actions.push_back({"Create Empty Child", [scene, entity]()
            {
                auto child = scene->AddObject("GameObject");
                scene->Reparent(child, entity);
                Selection::SetSelection(child);
            }});

            if (has_parent)
            {
                actions.push_back({"Detach", [scene, entity]()
                {
                    scene->Detach(entity, true);
                }});
            }

            actions.push_back({"Delete", [scene, selected_entities, entity_name]()
            {
                const String message = selected_entities.size() > 1u
                    ? std::format("Delete {} selected entities?", selected_entities.size())
                    : std::format("Delete '{}'?", entity_name);
                EditorPopup::ShowConfirmAt(Input::GetGlobalMousePos(),
                    message,
                    [scene, selected_entities]()
                    {
                        for (ECS::Entity selected_entity : selected_entities)
                        {
                            Selection::RemoveSlection(selected_entity);
                            if (scene->IsValidEntity(selected_entity))
                                scene->RemoveObject(selected_entity);
                        }
                    }, "Delete");
            }, true});

            EditorPopup::ShowActionMenuAt(position, actions);
        }

        void WorldOutline::UpdateSceneTitle(Scene *scene)
        {
            const bool is_temporary_prefab = SceneMgr::Get().IsTemporaryPrefabScene();
            if (_back_button != nullptr)
                _back_button->SetVisible(is_temporary_prefab);
            if (_scene_title == nullptr)
                return;

            if (scene == nullptr)
            {
                _scene_title->SetText("No Scene");
                return;
            }

            String title = scene->Name();
            if (is_temporary_prefab && SceneMgr::Get().HasTemporarySceneEdits())
                title.push_back('*');
            _scene_title->SetText(title);
        }

        void WorldOutline::ExitTemporaryPrefabScene()
        {
            auto &scene_mgr = SceneMgr::Get();
            if (!scene_mgr.IsTemporaryPrefabScene())
                return;

            Scene *scene = scene_mgr.ActiveScene();
            if (scene != nullptr && scene_mgr.HasTemporarySceneEdits())
            {
                const Guid &asset_guid = scene_mgr.TemporaryPrefabAssetGuid();
                const WString &asset_path = ResourceMgr::Get().GuidToAssetPath(asset_guid);
                Asset *asset = ResourceMgr::Get().GetAsset(asset_path);
                auto *prefab = asset != nullptr ? asset->As<PrefabAssetDocument>() : nullptr;
                if (asset == nullptr || prefab == nullptr ||
                    !SceneManagement::PrefabSystem::UpdatePrefabDocument(*scene, scene_mgr.TemporaryPrefabRootEntity(), *prefab))
                {
                    LOG_ERROR(L"WorldOutline: failed to update prefab document before leaving prefab edit mode");
                }
                else
                {
                    ResourceMgr::Get().MarkAssetDirty(asset);
                    if (!ResourceMgr::Get().SaveAsset(asset))
                        LOG_ERROR(L"WorldOutline: failed to save prefab asset {}", asset_path);
                }
            }

            Selection::RemoveSlection();
            scene_mgr.CloseTemporaryScene();
        }

        void WorldOutline::BeginEntityRename(ECS::Entity entity)
        {
            auto *scene = SceneMgr::Get().ActiveScene();
            if (scene == nullptr || !scene->IsValidEntity(entity))
                return;

            auto *tag = scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
            String initial = tag ? tag->_name : "";
            auto *row = _tree_view->GetRowForItem(SceneTreeDataSource::ToTreeItem(entity));
            if (row == nullptr || row->ChildAt(0u) == nullptr)
                return;

            auto *row_layout = row->ChildAt(0u)->As<HorizontalBox>();
            if (row_layout == nullptr || row_layout->GetChildren().empty())
                return;

            auto *label = row_layout->ChildAt(static_cast<u32>(row_layout->GetChildren().size() - 1u))->As<Text>();
            if (label == nullptr)
                return;

            EditorPopup::BeginInlineTextInput(row_layout, label, initial,
                [scene, entity](const String& value) -> std::optional<String>
                {
                    String name = value;
                    size_t b = 0, e = name.size();
                    while (b < e && std::isspace(static_cast<unsigned char>(name[b]))) ++b;
                    while (e > b && std::isspace(static_cast<unsigned char>(name[e - 1]))) --e;
                    name = name.substr(b, e - b);
                    if (name.empty()) return String("Name cannot be empty.");
                    if (name.size() > 256) return String("Name too long (max 256).");
                    scene->RenameEntity(entity, name);
                    return std::nullopt;
                });
        }

        void WorldOutline::FocusCameraOnEntity(ECS::Entity entity)
        {
            auto* scene = SceneMgr::Get().ActiveScene();
            if (!scene || scene != _observed_scene || !scene->IsValidEntity(entity)) return;

            auto& reg = scene->GetRegister();
            auto* transform = reg.GetComponent<ECS::TransformComponent>(entity);
            if (!transform) return;

            Vector3f target_pos = transform->_local_transform._position;
            f32 distance = 2.0f;

            if (auto* sm = reg.GetComponent<ECS::StaticMeshComponent>(entity))
            {
                if (!sm->_transformed_aabbs.empty())
                    distance = sm->_transformed_aabbs[0].Diagon() * 1.1f;
            }

            auto& cam_controller = dynamic_cast<EditorApp&>(Application::Get()).GetSceneCameraController();
            if (Camera::sCurrent)
            {
                target_pos += -Camera::sCurrent->Forward() * distance;
                cam_controller.SetTargetPosition(target_pos, true);
            }
        }

        void WorldOutline::OnDropAction(UI::TreeView*, u64 source_item, u64 target_item)
        {
            auto* scene = SceneMgr::Get().ActiveScene();
            if (!scene) return;

            auto src_entity = SceneTreeDataSource::ToEntity(source_item);
            if (!scene->IsValidEntity(src_entity)) return;

            ECS::Entity new_parent = ECS::kInvalidEntity;
            if (target_item != UI::kInvalidTreeItemId)
            {
                new_parent = SceneTreeDataSource::ToEntity(target_item);
                if (!scene->IsValidEntity(new_parent))
                    return;
            }

            g_pCommandMgr->ExecuteCommand(MakeScope<SceneQueuedCommand>(
                scene,
                MakeScope<SceneManagement::ReparentSceneCommand>(src_entity, new_parent, true)));

            _observed_structure_revision = scene->StructureRevision();
            _tree_view->Refresh();
            _tree_view->SetSelectedItem(source_item, false);
        }

    }// namespace Editor
}// namespace Ailu
