#include "Editors/SkeletonMeshAssetEditor.h"

#include "Animation/Clip.h"
#include "Animation/SkeletonAsset.h"
#include "Assets/Asset.h"
#include "Common/AssetEditorLayout.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Mesh.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/ObjectAssetDropdown.h"
#include "UI/TreeView.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Ailu::Editor
{
    class SkeletonMeshAssetEditor::SkeletonTreeDataSource final : public UI::ITreeViewDataSource
    {
    public:
        static UI::TreeItemId ToTreeItem(u16 joint_index)
        {
            return static_cast<UI::TreeItemId>(joint_index) + 1u;
        }

        static u16 ToJointIndex(UI::TreeItemId item)
        {
            return item == UI::kInvalidTreeItemId ? Joint::kInvalidJointIndex : static_cast<u16>(item - 1u);
        }

        void SetMesh(Render::SkeletonMesh *mesh) { _mesh = mesh; }

        Vector<UI::TreeItemId> GetRootItems() const override
        {
            Vector<UI::TreeItemId> roots;
            if (_mesh == nullptr)
                return roots;
            if (!_mesh->GetSkeletonAsset().IsResolved())
                return roots;
            const Skeleton &skeleton = _mesh->GetSkeletonAsset()->GetSkeleton();
            for (u16 index = 0u; index < skeleton.JointNum(); ++index)
            {
                const u16 parent = skeleton[index]._parent;
                if (parent == Joint::kInvalidJointIndex || parent >= skeleton.JointNum())
                    roots.emplace_back(ToTreeItem(index));
            }
            return roots;
        }

        Vector<UI::TreeItemId> GetChildren(UI::TreeItemId parent) const override
        {
            Vector<UI::TreeItemId> children;
            if (_mesh == nullptr)
                return children;
            if (!_mesh->GetSkeletonAsset().IsResolved())
                return children;
            const Skeleton &skeleton = _mesh->GetSkeletonAsset()->GetSkeleton();
            const u16 parent_index = ToJointIndex(parent);
            if (parent_index == Joint::kInvalidJointIndex || parent_index >= skeleton.JointNum())
                return children;
            for (u16 index = 0u; index < skeleton.JointNum(); ++index)
            {
                if (skeleton[index]._parent == parent_index)
                    children.emplace_back(ToTreeItem(index));
            }
            return children;
        }

        UI::TreeItemId GetParent(UI::TreeItemId item) const override
        {
            if (_mesh == nullptr)
                return UI::kInvalidTreeItemId;
            if (!_mesh->GetSkeletonAsset().IsResolved())
                return UI::kInvalidTreeItemId;
            const Skeleton &skeleton = _mesh->GetSkeletonAsset()->GetSkeleton();
            const u16 joint_index = ToJointIndex(item);
            if (joint_index == Joint::kInvalidJointIndex || joint_index >= skeleton.JointNum())
                return UI::kInvalidTreeItemId;
            const u16 parent = skeleton[joint_index]._parent;
            return parent == Joint::kInvalidJointIndex || parent >= skeleton.JointNum() ? UI::kInvalidTreeItemId :
                                                                                             ToTreeItem(parent);
        }

        UI::TreeItemPresentation GetPresentation(UI::TreeItemId item) const override
        {
            UI::TreeItemPresentation presentation;
            const u16 joint_index = ToJointIndex(item);
            if (_mesh == nullptr || joint_index == Joint::kInvalidJointIndex ||
                !_mesh->GetSkeletonAsset().IsResolved() || joint_index >= _mesh->GetSkeletonAsset()->GetSkeleton().JointNum())
            {
                presentation._label = "<Invalid>";
                presentation._selectable = false;
                return presentation;
            }
            const Joint &joint = _mesh->GetSkeletonAsset()->GetSkeleton()[joint_index];
            presentation._label = joint._name.empty() ? std::format("Joint {}", joint_index) : joint._name;
            presentation._selectable = true;
            return presentation;
        }

        bool IsValid(UI::TreeItemId item) const override
        {
            return _mesh != nullptr && _mesh->GetSkeletonAsset().IsResolved() &&
                   ToJointIndex(item) < _mesh->GetSkeletonAsset()->GetSkeleton().JointNum();
        }

    private:
        Render::SkeletonMesh *_mesh = nullptr;
    };

    namespace
    {
        const Color kPanelColor = Color(0.16f, 0.17f, 0.19f, 1.0f);
        const Color kCenterColor = Color(0.08f, 0.09f, 0.10f, 1.0f);
        const Color kTextColor = Color(0.78f, 0.80f, 0.84f, 1.0f);
        const Color kMutedTextColor = Color(0.56f, 0.58f, 0.62f, 1.0f);

        void StyleText(UI::Text *text, Color color = kTextColor, f32 size = 11.0f)
        {
            text->_color = color;
            text->FontSize(size);
        }

        UI::Text *AddInfoRow(UI::VerticalBox *parent, const String &label)
        {
            auto *row = AssetEditorLayout::AddPropertyRow(parent, label, 100.0f);
            auto *value = row->AddChild<UI::Text>("-");
            StyleText(value);
            value->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            return value;
        }

        String FormatVector(const Vector3f &value)
        {
            return std::format("({:.3f}, {:.3f}, {:.3f})", value.x, value.y, value.z);
        }
    }

    SkeletonMeshAssetEditor::SkeletonMeshAssetEditor() : AssetEditor("Skeleton Mesh Editor", {1240.0f, 760.0f})
    {
        SetPosition({80.0f, 45.0f});
        _data_source = MakeScope<SkeletonTreeDataSource>();
        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 30.0f});
        BuildToolbar(toolbar);

        auto *main = root->AddChild<UI::SplitView>();
        main->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        main->SetRatio(0.24f);
        auto *tree_border = main->AddChild<UI::Border>();
        tree_border->_bg_color = kPanelColor;
        auto *tree = tree_border->AddChild<UI::VerticalBox>();
        tree->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        BuildSkeletonTree(tree);

        auto *center_right = main->AddChild<UI::SplitView>();
        center_right->SetRatio(0.72f);
        auto *center = center_right->AddChild<UI::Border>();
        center->_bg_color = kCenterColor;
        center->_border_color = Color(0.30f, 0.34f, 0.40f, 1.0f);
        center->Thickness(1.0f);
        _preview_image = center->AddChild<UI::Image>();
        _preview_image->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin({3.0f, 3.0f, 3.0f, 3.0f});
        _preview_image->SetWantsMouseEvents(true);
        _preview_image->SetInteractiveEnabled(true);
        _preview_image->OnMouseDown() += [this](UI::UIEvent &event)
        {
            if (_preview_image == nullptr)
                return;
            const Vector4f rect = _preview_image->GetArrangeRect();
            if (event._key_code == EKey::kLBUTTON)
            {
                _preview_mouse_down_position = event._mouse_position;
                _preview_camera_dragged = false;
                _preview.BeginCameraDrag(event._mouse_position - rect.xy);
            }
            else if (event._key_code == EKey::kRBUTTON)
                _preview.BeginCameraPan(event._mouse_position - rect.xy);
            event._is_handled = true;
        };
        _preview_image->OnMouseUp() += [this](UI::UIEvent &event)
        {
            if (event._key_code == EKey::kLBUTTON)
            {
                _preview.EndCameraDrag();
                if (!_preview_camera_dragged)
                {
                    const Vector4f rect = _preview_image->GetArrangeRect();
                    u16 joint_index = Joint::kInvalidJointIndex;
                    _preview.PickJoint(event._mouse_position - rect.xy, joint_index);
                    SelectJoint(joint_index);
                    RefreshSkeletonTree();
                }
            }
            else if (event._key_code == EKey::kRBUTTON)
                _preview.EndCameraPan();
            event._is_handled = true;
        };
        _preview_image->OnMouseMove() += [this](UI::UIEvent &event)
        {
            if (_preview_image == nullptr)
                return;
            const Vector4f rect = _preview_image->GetArrangeRect();
            const Vector2f local_position = event._mouse_position - rect.xy;
            if (Input::IsKeyDown(EKey::kLBUTTON))
            {
                const Vector2f delta = event._mouse_position - _preview_mouse_down_position;
                _preview_camera_dragged = _preview_camera_dragged ||
                                          delta.x * delta.x + delta.y * delta.y > 16.0f;
            }
            _preview.DragCamera(local_position);
            _preview.PanCamera(local_position);
            event._is_handled = true;
        };
        _preview_image->OnMouseScroll() += [this](UI::UIEvent &event)
        {
            _preview.ZoomCamera(event._scroll_delta);
            event._is_handled = true;
        };

        auto *info_border = center_right->AddChild<UI::Border>();
        info_border->_bg_color = kPanelColor;
        auto *info_scroll = info_border->AddChild<UI::ScrollView>();
        info_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *info = info_scroll->AddChild<UI::VerticalBox>();
        info->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        info->SlotPadding() = UI::Padding(5.0f);
        BuildInfoPanel(info);
    }

    SkeletonMeshAssetEditor::~SkeletonMeshAssetEditor() = default;

    void SkeletonMeshAssetEditor::BuildToolbar(UI::HorizontalBox *toolbar)
    {
        AddAssetMenu(toolbar);
        auto add_button = [toolbar](const String &text, f32 width)
        {
            auto *button = toolbar->AddChild<UI::Button>(text);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size({width, 0.0f}).Margin({0.0f, 0.0f, 2.0f, 0.0f});
            return button;
        };
        auto *focus = add_button("Focus", 52.0f);
        focus->OnMouseClick() += [this](UI::UIEvent &event) { _preview.ResetCamera(); event._is_handled = true; };
        _btn_grid = add_button("Grid: On", 66.0f);
        _btn_grid->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _show_grid = !_show_grid;
            if (_btn_grid) _btn_grid->SetText(_show_grid ? "Grid: On" : "Grid: Off");
            _preview.SetShowGrid(_show_grid);
            event._is_handled = true;
        };
        _btn_skeleton = add_button("Skeleton: On", 92.0f);
        _btn_skeleton->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _show_skeleton = !_show_skeleton;
            _preview.SetShowSkeleton(_show_skeleton);
            if (_btn_skeleton) _btn_skeleton->SetText(_show_skeleton ? "Skeleton: On" : "Skeleton: Off");
            event._is_handled = true;
        };
        _btn_wireframe = add_button("Wire: Off", 72.0f);
        _btn_wireframe->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _wireframe = !_wireframe;
            if (_btn_wireframe) _btn_wireframe->SetText(_wireframe ? "Wire: On" : "Wire: Off");
            _preview.SetWireframe(_wireframe);
            event._is_handled = true;
        };
        _clip_dropdown = toolbar->AddChild<UI::Dropdown>();
        _clip_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size({180.0f, 0.0f}).Margin({4.0f, 0.0f, 2.0f, 0.0f});
        _clip_dropdown->_on_selected_changed += [this](i32 index) { SelectClip(index); };
        _btn_play = add_button("Play", 52.0f);
        _btn_play->OnMouseClick() += [this](UI::UIEvent &event) { TogglePlayback(); event._is_handled = true; };
        auto *stop = add_button("Stop", 52.0f);
        stop->OnMouseClick() += [this](UI::UIEvent &event) { StopPlayback(); event._is_handled = true; };
        _txt_time = toolbar->AddChild<UI::Text>("0.000s");
        StyleText(_txt_time, kMutedTextColor);
        _txt_time->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin({8.0f, 0.0f, 0.0f, 0.0f});
    }

    void SkeletonMeshAssetEditor::BuildSkeletonTree(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Skeleton");
        _skeleton_tree = panel->AddChild<UI::TreeView>();
        _skeleton_tree->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        _skeleton_tree->_row_height = 24.0f;
        _skeleton_tree->_indent_width = 14.0f;
        _skeleton_tree->SetDataSource(_data_source.get());
        _skeleton_tree->_on_selection_changed += [this](UI::TreeItemId item)
        {
            const u16 joint_index = SkeletonTreeDataSource::ToJointIndex(item);
            if (joint_index != Joint::kInvalidJointIndex)
                SelectJoint(joint_index);
        };
    }

    void SkeletonMeshAssetEditor::BuildInfoPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Mesh");
        _txt_asset_name = AddInfoRow(panel, "Name");
        _txt_vertex_count = AddInfoRow(panel, "Vertices");
        _txt_triangle_count = AddInfoRow(panel, "Triangles");
        AssetEditorLayout::AddSectionTitle(panel, "Skeleton");
        auto *skeleton_row = AssetEditorLayout::AddPropertyRow(panel, "Asset", 100.0f);
        _skeleton_dropdown = skeleton_row->AddChild<UI::ObjectAssetDropdown>(SkeletonAsset::StaticType());
        _skeleton_dropdown->SetAllowNone(true);
        _skeleton_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
            .Margin(Vector4f(2.0f, 1.0f, 2.0f, 1.0f));
        _skeleton_dropdown->_on_object_asset_selected += [this](Asset *, Object *, const Guid &guid)
        {
            SelectSkeletonAsset(guid);
        };
        _txt_skeleton_guid = AddInfoRow(panel, "Guid");
        _txt_bone_count = AddInfoRow(panel, "Joints");
        _txt_skeleton_layout = AddInfoRow(panel, "Layout Hash");
        AssetEditorLayout::AddSectionTitle(panel, "Selected Bone");
        _txt_selected_bone = AddInfoRow(panel, "Name");
        _txt_parent = AddInfoRow(panel, "Parent");
        _txt_children = AddInfoRow(panel, "Children");
        _txt_bind_position = AddInfoRow(panel, "Bind Position");
    }

    void SkeletonMeshAssetEditor::Update(f32 dt)
    {
        AssetEditor::Update(dt);
        if (_mesh == nullptr)
            return;
        if (_playing && _clip != nullptr && _clip->Duration() > 0.0f)
        {
            _preview_time += dt;
            if (_clip->IsLooping())
                _preview_time = std::fmod(_preview_time, _clip->Duration());
            else if (_preview_time >= _clip->Duration())
            {
                _preview_time = _clip->Duration();
                _playing = false;
            }
            _preview.SetTime(_preview_time);
        }
        if (_preview_image != nullptr)
        {
            const Vector4f rect = _preview_image->GetArrangeRect();
            if (_preview.SetViewportSize({rect.z, rect.w}))
                _preview.SetTime(_preview_time);
            _preview.RenderIfPending();
            _preview_image->SetTexture(_preview.GetRenderTexture());
        }
        if (_txt_time) _txt_time->SetText(std::format("{:.3f}s", _preview_time));
    }

    bool SkeletonMeshAssetEditor::OnOpen()
    {
        _mesh = GetAssetObject<Render::SkeletonMesh>();
        if (_mesh == nullptr)
            return false;
        _clips.clear();
        _clip_asset_paths.clear();
        Vector<String> clip_names;
        clip_names.emplace_back("Bind Pose");
        for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
        {
            Asset *asset = it->second.get();
            if (asset == nullptr || asset->_asset_type != AnimationClip::StaticType())
                continue;
            _clip_asset_paths.emplace_back(asset->_asset_path);
            _clips.emplace_back(nullptr);
            clip_names.emplace_back(asset->Name());
        }
        if (_clip_dropdown != nullptr)
        {
            _clip_dropdown->SetItems(clip_names);
            _clip_dropdown->SetSelectedIndex(0);
        }
        _clip = nullptr;
        _preview_time = 0.0f;
        _playing = false;
        _selected_joint = Joint::kInvalidJointIndex;
        _preview.SetMesh(_mesh);
        _preview.SetShowGrid(_show_grid);
        _preview.SetShowSkeleton(_show_skeleton);
        _preview.SetWireframe(_wireframe);
        _preview.SetSelectedJoint(_selected_joint);
        RefreshAllUI();
        _preview.SetTime(0.0f);
        return true;
    }

    void SkeletonMeshAssetEditor::OnClose()
    {
        _mesh = nullptr;
        _clip = nullptr;
        _clips.clear();
        _clip_asset_paths.clear();
        _preview.SetMesh(nullptr);
        if (_data_source != nullptr)
        {
            _data_source->SetMesh(nullptr);
            if (_skeleton_tree != nullptr)
                _skeleton_tree->Refresh();
        }
    }

    void SkeletonMeshAssetEditor::OnAssetReloaded()
    {
        _preview.SetMesh(nullptr);
        OnOpen();
    }

    void SkeletonMeshAssetEditor::RefreshAllUI()
    {
        RefreshSkeletonTree();
        RefreshBoneInfo();
        if (_txt_asset_name) _txt_asset_name->SetText(_mesh != nullptr ? _mesh->Name() : "-");
        if (_txt_vertex_count) _txt_vertex_count->SetText(_mesh != nullptr ? std::format("{}", _mesh->GetVertexCount()) : "-");
        if (_txt_triangle_count) _txt_triangle_count->SetText(_mesh != nullptr ? std::format("{}", _mesh->GetTriangleCount()) : "-");
        if (_skeleton_dropdown != nullptr)
            _skeleton_dropdown->SetSelectedGuid(_mesh != nullptr ? _mesh->GetSkeletonAsset().GetGuid() : Guid::EmptyGuid());
        const Ref<SkeletonAsset> skeleton_asset = _mesh != nullptr ? _mesh->GetSkeletonAsset().Get() : nullptr;
        if (_txt_skeleton_guid)
        {
            const AssetRef<SkeletonAsset> *skeleton_ref = _mesh != nullptr ? &_mesh->GetSkeletonAsset() : nullptr;
            _txt_skeleton_guid->SetText(skeleton_ref != nullptr && skeleton_ref->IsAssigned() ?
                                            skeleton_ref->GetGuid().ToString() : "-");
        }
        if (_txt_bone_count) _txt_bone_count->SetText(skeleton_asset != nullptr ?
                                                       std::format("{}", skeleton_asset->GetSkeleton().JointNum()) : "-");
        if (_txt_skeleton_layout) _txt_skeleton_layout->SetText(skeleton_asset != nullptr ?
                                                                 std::format("{}", skeleton_asset->LayoutHash()) : "-");
    }

    void SkeletonMeshAssetEditor::RefreshSkeletonTree()
    {
        if (_skeleton_tree == nullptr)
            return;
        _data_source->SetMesh(_mesh);
        _skeleton_tree->Refresh();
        if (_mesh == nullptr || _selected_joint == Joint::kInvalidJointIndex ||
            !_mesh->GetSkeletonAsset().IsResolved() || _selected_joint >= _mesh->GetSkeletonAsset()->GetSkeleton().JointNum())
        {
            _skeleton_tree->ClearSelection(false);
            return;
        }
        const UI::TreeItemId item = SkeletonTreeDataSource::ToTreeItem(_selected_joint);
        _skeleton_tree->ExpandParents(item);
        _skeleton_tree->SetSelectedItem(item, false);
        _skeleton_tree->ScrollItemIntoView(item);
    }

    void SkeletonMeshAssetEditor::RefreshBoneInfo()
    {
        if (_mesh == nullptr || !_mesh->GetSkeletonAsset().IsResolved() || _selected_joint == Joint::kInvalidJointIndex ||
            _selected_joint >= _mesh->GetSkeletonAsset()->GetSkeleton().JointNum())
        {
            if (_txt_selected_bone) _txt_selected_bone->SetText("None");
            if (_txt_parent) _txt_parent->SetText("-");
            if (_txt_children) _txt_children->SetText("-");
            if (_txt_bind_position) _txt_bind_position->SetText("-");
            return;
        }
        const Skeleton &skeleton = _mesh->GetSkeletonAsset()->GetSkeleton();
        const Joint &joint = skeleton[_selected_joint];
        const String parent_name = joint._parent == Joint::kInvalidJointIndex ? "Root" :
            (joint._parent < skeleton.JointNum() ? skeleton[joint._parent]._name : "-");
        const Transform transform = skeleton.GetBindPose().GetLocalTransform(_selected_joint);
        if (_txt_selected_bone) _txt_selected_bone->SetText(joint._name);
        if (_txt_parent) _txt_parent->SetText(parent_name);
        if (_txt_children) _txt_children->SetText(std::format("{}", joint._children.size()));
        if (_txt_bind_position) _txt_bind_position->SetText(FormatVector(transform._position));
    }

    void SkeletonMeshAssetEditor::RefreshPreview()
    {
        _preview.SetShowGrid(_show_grid);
        _preview.SetShowSkeleton(_show_skeleton);
        _preview.SetWireframe(_wireframe);
        _preview.SetSelectedJoint(_selected_joint);
        _preview.SetTime(_preview_time);
    }

    void SkeletonMeshAssetEditor::SelectJoint(u16 joint_index)
    {
        _selected_joint = joint_index;
        _preview.SetSelectedJoint(_selected_joint);
        RefreshBoneInfo();
    }

    void SkeletonMeshAssetEditor::SelectSkeletonAsset(const Guid &guid)
    {
        if (_mesh == nullptr)
            return;
        Ref<SkeletonAsset> skeleton_asset;
        if (!guid.IsEmpty())
        {
            skeleton_asset = ResourceMgr::Get().GetRef<SkeletonAsset>(guid);
            if (skeleton_asset == nullptr)
                skeleton_asset = ResourceMgr::Get().Load<SkeletonAsset>(guid);
        }
        _mesh->SetSkeletonAsset(guid, std::move(skeleton_asset));
        _selected_joint = Joint::kInvalidJointIndex;
        _preview.SetMesh(nullptr);
        _preview.SetMesh(_mesh);
        _preview.SetTime(_preview_time);
        if (GetAsset() != nullptr && !GetAsset()->IsDirty())
            GetAsset()->MarkModified();
        RefreshAllUI();
    }

    void SkeletonMeshAssetEditor::SelectClip(i32 index)
    {
        if (index <= 0 || static_cast<size_t>(index - 1) >= _clips.size())
        {
            _clip = nullptr;
            _preview.SetClip(nullptr);
        }
        else
        {
            const size_t clip_index = static_cast<size_t>(index - 1);
            if (_clips[clip_index] == nullptr && clip_index < _clip_asset_paths.size())
                _clips[clip_index] = ResourceMgr::Get().Load<AnimationClip>(_clip_asset_paths[clip_index]);
            _clip = _clips[clip_index].get();
            _preview.SetClip(_clip);
        }
        _preview_time = 0.0f;
        _preview.SetTime(0.0f);
        _playing = false;
        if (_btn_play) _btn_play->SetText("Play");
    }

    void SkeletonMeshAssetEditor::TogglePlayback()
    {
        if (_clip == nullptr || _clip->Duration() <= 0.0f)
            return;
        _playing = !_playing;
        if (_btn_play) _btn_play->SetText(_playing ? "Pause" : "Play");
    }

    void SkeletonMeshAssetEditor::StopPlayback()
    {
        _playing = false;
        _preview_time = 0.0f;
        _preview.SetTime(0.0f);
        if (_btn_play) _btn_play->SetText("Play");
    }
}
