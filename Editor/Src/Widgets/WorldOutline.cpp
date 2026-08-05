#include "Widgets/WorldOutline.h"
#include "Common/Selection.h"
#include "Common/Undo.h"
#include "Common/EditorPopup.h"
#include "Common/CameraControllers.h"
#include "EditorApp.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Input.h"
#include "Framework/Events/Event.h"
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
                _scene_title->SetText(scene->Name());
            }
            else
            {
                _scene_title->SetText("No Scene");
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
            _tree_view->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kFill);
        }

        void WorldOutline::BindTreeEvents()
        {
            _tree_view->_on_selection_changed += [this](TreeItemId item)
            {
                auto entity = SceneTreeDataSource::ToEntity(item);
                auto* scene = SceneMgr::Get().ActiveScene();
                if (scene && scene->IsValidEntity(entity))
                {
                    Selection::SetSelection(entity);
                }
            };

            _tree_view->_on_item_double_clicked += [this](TreeItemId item)
            {
                FocusCameraOnEntity(SceneTreeDataSource::ToEntity(item));
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
            auto* scene = SceneMgr::Get().ActiveScene();

            if (scene != _observed_scene)
            {
                _observed_scene = scene;
                _data_source->SetScene(scene);
                _tree_view->SetDataSource(scene ? _data_source.get() : nullptr);
                _observed_structure_revision = scene ? scene->StructureRevision() : 0;
                if (scene)
                {
                    _scene_title->SetText(scene->Name());
                    SyncSelectionFromEngine();
                }
                else
                {
                    _scene_title->SetText("No Scene");
                }
            }
            else if (scene)
            {
                u64 rev = scene->StructureRevision();
                if (rev != _observed_structure_revision)
                {
                    _observed_structure_revision = rev;
                    _tree_view->Refresh();
                }
            }

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
            ECS::Entity entity = Selection::FirstEntity();

            if (!scene || !scene->IsValidEntity(entity))
            {
                _tree_view->ClearSelection(false);
            }
            else
            {
                auto item = SceneTreeDataSource::ToTreeItem(entity);
                _tree_view->ExpandParents(item);
                _tree_view->SetSelectedItem(item, false);
                _tree_view->ScrollItemIntoView(item);
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

            Vector<PopupMenuAction> actions;

            actions.push_back({"Rename", [scene, entity]()
            {
                auto* tag = scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
                String initial = tag ? tag->_name : "";
                EditorPopup::ShowTextInputAt(Input::GetGlobalMousePos(), "Rename Entity", initial,
                    [scene, entity](const String& value) -> std::optional<String>
                    {
                        String name = value;
                        // Trim
                        size_t b = 0, e = name.size();
                        while (b < e && std::isspace(static_cast<unsigned char>(name[b]))) ++b;
                        while (e > b && std::isspace(static_cast<unsigned char>(name[e - 1]))) --e;
                        name = name.substr(b, e - b);
                        if (name.empty()) return String("Name cannot be empty.");
                        if (name.size() > 256) return String("Name too long (max 256).");
                        scene->RenameEntity(entity, name);
                        return std::nullopt;
                    });
            }});

            actions.push_back({"Duplicate", [scene, entity]()
            {
                scene->DuplicateEntity(entity);
            }});

            actions.push_back({"Copy Entity GUID", [scene, entity]()
            {
                ImGui::SetClipboardText(scene->GetEntityGuid(entity).ToString().c_str());
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

            actions.push_back({"Delete", [scene, entity, entity_name]()
            {
                EditorPopup::ShowConfirmAt(Input::GetGlobalMousePos(),
                    std::format("Delete '{}'?", entity_name),
                    [scene, entity]()
                    {
                        Selection::RemoveSlection(entity);
                        scene->RemoveObject(entity);
                    }, "Delete");
            }, true});

            EditorPopup::ShowActionMenuAt(position, actions);
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
