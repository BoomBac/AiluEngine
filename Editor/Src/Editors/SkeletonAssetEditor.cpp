#include "Editors/SkeletonAssetEditor.h"

#include "Animation/SkeletonAsset.h"
#include "Assets/Asset.h"
#include "Common/AssetEditorLayout.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/CommandBuffer.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/ResourcePool.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/TreeView.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace Ailu::Editor
{
    class SkeletonAssetEditor::SkeletonTreeDataSource final : public UI::ITreeViewDataSource
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

        void SetSkeletonAsset(SkeletonAsset *skeleton_asset) { _skeleton_asset = skeleton_asset; }

        Vector<UI::TreeItemId> GetRootItems() const override
        {
            Vector<UI::TreeItemId> roots;
            if (_skeleton_asset == nullptr)
                return roots;
            const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
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
            if (_skeleton_asset == nullptr)
                return children;
            const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
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
            if (_skeleton_asset == nullptr)
                return UI::kInvalidTreeItemId;
            const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
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
            if (_skeleton_asset == nullptr || joint_index == Joint::kInvalidJointIndex ||
                joint_index >= _skeleton_asset->GetSkeleton().JointNum())
            {
                presentation._label = "<Invalid>";
                presentation._selectable = false;
                return presentation;
            }
            const Joint &joint = _skeleton_asset->GetSkeleton()[joint_index];
            presentation._label = joint._name.empty() ? std::format("Joint {}", joint_index) : joint._name;
            presentation._selectable = true;
            return presentation;
        }

        bool IsValid(UI::TreeItemId item) const override
        {
            return _skeleton_asset != nullptr && ToJointIndex(item) < _skeleton_asset->GetSkeleton().JointNum();
        }

    private:
        SkeletonAsset *_skeleton_asset = nullptr;
    };

    namespace
    {
        const Color kPanelColor = Color(0.16f, 0.17f, 0.19f, 1.0f);
        const Color kTextColor = Color(0.78f, 0.80f, 0.84f, 1.0f);
        const Color kMutedTextColor = Color(0.56f, 0.58f, 0.62f, 1.0f);

        void StyleText(UI::Text *text, Color color = kTextColor, f32 size = 11.0f)
        {
            text->_color = color;
            text->FontSize(size);
        }

        UI::Text *AddInfoRow(UI::VerticalBox *parent, const String &label)
        {
            auto *row = AssetEditorLayout::AddPropertyRow(parent, label, 112.0f);
            auto *value = row->AddChild<UI::Text>("-");
            StyleText(value);
            value->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            return value;
        }

        String FormatVector(const Vector3f &value)
        {
            return std::format("({:.3f}, {:.3f}, {:.3f})", value.x, value.y, value.z);
        }

        String FormatQuaternion(const Quaternion &value)
        {
            return std::format("({:.3f}, {:.3f}, {:.3f}, {:.3f})", value.x, value.y, value.z, value.w);
        }

        bool GetMeshBounds(Render::Mesh *mesh, Vector3f &center, Vector3f &size)
        {
            if (mesh == nullptr || mesh->BoundBox().empty())
                return false;
            const AABB &bounds = mesh->BoundBox()[0];
            center = (bounds._min + bounds._max) * 0.5f;
            size = bounds._max - bounds._min;
            size.x = std::max(size.x, 0.001f);
            size.y = std::max(size.y, 0.001f);
            size.z = std::max(size.z, 0.001f);
            return true;
        }

        bool IsDrawableMesh(Render::Mesh *mesh)
        {
            return mesh != nullptr && mesh->GetVertexBuffer() != nullptr && mesh->SubmeshCount() > 0u &&
                   !mesh->GetIndices(0u).empty();
        }

        Matrix4x4f MakeAxisOrientation(const Vector3f &direction)
        {
            const Vector3f dir = Normalize(direction);
            const f32 dot = DotProduct(Vector3f::kUp, dir);
            if (dot > 1.0f - 1e-6f)
                return BuildIdentityMatrix();
            if (dot < -1.0f + 1e-6f)
                return MatrixRotationX(Math::kPi);
            const Vector3f axis = Normalize(CrossProduct(Vector3f::kUp, dir));
            Matrix4x4f orientation;
            MatrixRotationAxis(orientation, axis, std::acos(std::clamp(dot, -1.0f, 1.0f)));
            return orientation;
        }

        Matrix4x4f MakeJointMatrix(Render::Mesh *mesh, const Vector3f &position, f32 radius)
        {
            Vector3f center;
            Vector3f size;
            if (!GetMeshBounds(mesh, center, size))
                return MatrixTranslation(position);
            const Vector3f scale{radius * 2.0f / size.x, radius * 2.0f / size.y, radius * 2.0f / size.z};
            const Vector3f offset{-center.x * scale.x, -center.y * scale.y, -center.z * scale.z};
            return MatrixScale(scale) * MatrixTranslation(offset) * MatrixTranslation(position);
        }

        Matrix4x4f MakeBoneMatrix(Render::Mesh *mesh, const Vector3f &origin, const Vector3f &direction,
                                  f32 radius)
        {
            Vector3f center;
            Vector3f size;
            if (!GetMeshBounds(mesh, center, size))
                return MatrixTranslation(origin);
            const AABB &bounds = mesh->BoundBox()[0];
            const f32 length = Magnitude(direction);
            const Vector3f scale{radius * 2.0f / size.x, length / size.y, radius * 2.0f / size.z};
            const Vector3f offset{-center.x * scale.x, -bounds._min.y * scale.y, -center.z * scale.z};
            return MatrixScale(scale) * MatrixTranslation(offset) * MakeAxisOrientation(direction) *
                   MatrixTranslation(origin);
        }

        f32 DistanceSquaredToScreenSegment(const Vector2f &point, const Vector2f &segment_start,
                                           const Vector2f &segment_end, f32 &segment_factor)
        {
            const Vector2f segment = segment_end - segment_start;
            const f32 segment_length_squared = DotProduct(segment, segment);
            segment_factor = 0.0f;
            if (segment_length_squared > Math::kFloatEpsilon)
                segment_factor = std::clamp(DotProduct(point - segment_start, segment) /
                                                segment_length_squared,
                                            0.0f, 1.0f);
            const Vector2f difference = point - (segment_start + segment * segment_factor);
            return DotProduct(difference, difference);
        }
    }

    SkeletonAssetEditor::SkeletonAssetEditor() : AssetEditor("Skeleton Asset Editor", {760.0f, 620.0f})
    {
        SetPosition({180.0f, 80.0f});
        _data_source = MakeScope<SkeletonTreeDataSource>();
        _preview.SetOverlayCallback([this](Render::CommandBuffer *command_buffer, const Matrix4x4f &world_matrix,
                                            const Vector3f &center, const Vector3f &extents)
        {
            DrawSkeletonOverlay(command_buffer, world_matrix, center, extents);
        });

        auto *main = _content_root->AddChild<UI::SplitView>();
        main->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        main->SetRatio(0.25f);

        auto *tree_border = main->AddChild<UI::Border>();
        tree_border->_bg_color = kPanelColor;
        auto *tree_panel = tree_border->AddChild<UI::VerticalBox>();
        tree_panel->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        BuildSkeletonTree(tree_panel);

        auto *center_right = main->AddChild<UI::SplitView>();
        center_right->SetRatio(0.68f);
        auto *preview_border = center_right->AddChild<UI::Border>();
        preview_border->_bg_color = {0.08f, 0.09f, 0.10f, 1.0f};
        preview_border->_border_color = {0.30f, 0.34f, 0.40f, 1.0f};
        preview_border->Thickness(1.0f);
        _preview_image = preview_border->AddChild<UI::Image>();
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
                    if (PickJoint(event._mouse_position - rect.xy, joint_index))
                    {
                        SelectJoint(joint_index);
                        RefreshSkeletonTree();
                    }
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
                _preview_camera_dragged = _preview_camera_dragged || delta.x * delta.x + delta.y * delta.y > 16.0f;
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
        auto *info_panel = info_scroll->AddChild<UI::VerticalBox>();
        info_panel->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        info_panel->SlotPadding() = UI::Padding(8.0f);
        BuildInfoPanel(info_panel);
    }

    SkeletonAssetEditor::~SkeletonAssetEditor() = default;

    void SkeletonAssetEditor::Update(f32 dt)
    {
        AssetEditor::Update(dt);
        if (_preview_image == nullptr)
            return;
        const Vector4f rect = _preview_image->GetArrangeRect();
        if (_preview.SetViewportSize({rect.z, rect.w}) || _preview.IsRenderPending())
            _preview.Render();
        _preview_image->SetTexture(_preview.GetRenderTexture());
    }

    void SkeletonAssetEditor::BuildSkeletonTree(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Skeleton Hierarchy");
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

    void SkeletonAssetEditor::BuildInfoPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Skeleton Asset");
        _txt_asset_name = AddInfoRow(panel, "Name");
        _txt_asset_guid = AddInfoRow(panel, "GUID");
        _txt_joint_count = AddInfoRow(panel, "Joints");
        _txt_layout_hash = AddInfoRow(panel, "Layout Hash");
        AssetEditorLayout::AddSectionTitle(panel, "Selected Joint");
        _txt_selected_joint = AddInfoRow(panel, "Name");
        _txt_parent = AddInfoRow(panel, "Parent");
        _txt_children = AddInfoRow(panel, "Children");
        _txt_bind_position = AddInfoRow(panel, "Bind Position");
        _txt_bind_rotation = AddInfoRow(panel, "Bind Rotation");
        _txt_bind_scale = AddInfoRow(panel, "Bind Scale");
        StyleText(_txt_asset_guid, kMutedTextColor, 10.0f);
        StyleText(_txt_layout_hash, kMutedTextColor, 10.0f);
    }

    bool SkeletonAssetEditor::OnOpen()
    {
        _skeleton_asset = GetAssetObject<SkeletonAsset>();
        if (_skeleton_asset == nullptr)
            return false;
        _selected_joint = Joint::kInvalidJointIndex;
        RefreshAllUI();
        return true;
    }

    void SkeletonAssetEditor::OnClose()
    {
        _skeleton_asset = nullptr;
        _selected_joint = Joint::kInvalidJointIndex;
        if (_data_source != nullptr)
            _data_source->SetSkeletonAsset(nullptr);
        if (_skeleton_tree != nullptr)
            _skeleton_tree->Refresh();
        _preview.SetMesh(nullptr);
        RefreshJointInfo();
    }

    void SkeletonAssetEditor::OnAssetReloaded()
    {
        OnOpen();
    }

    void SkeletonAssetEditor::RefreshAllUI()
    {
        if (_txt_asset_name != nullptr)
            _txt_asset_name->SetText(_skeleton_asset != nullptr ? _skeleton_asset->Name() : "-");
        if (_txt_asset_guid != nullptr)
            _txt_asset_guid->SetText(GetAsset() != nullptr ? GetAsset()->GetGuid().ToString() : "-");
        if (_txt_joint_count != nullptr)
            _txt_joint_count->SetText(_skeleton_asset != nullptr ?
                                          std::format("{}", _skeleton_asset->GetSkeleton().JointNum()) : "-");
        if (_txt_layout_hash != nullptr)
            _txt_layout_hash->SetText(_skeleton_asset != nullptr ?
                                          std::format("{}", _skeleton_asset->LayoutHash()) : "-");
        RefreshSkeletonTree();
        RefreshJointInfo();
        RefreshPreview();
    }

    void SkeletonAssetEditor::RefreshSkeletonTree()
    {
        if (_skeleton_tree == nullptr || _data_source == nullptr)
            return;
        _data_source->SetSkeletonAsset(_skeleton_asset);
        _skeleton_tree->Refresh();
        if (_skeleton_asset == nullptr || _selected_joint == Joint::kInvalidJointIndex ||
            _selected_joint >= _skeleton_asset->GetSkeleton().JointNum())
        {
            _skeleton_tree->ClearSelection(false);
            return;
        }
        const UI::TreeItemId item = SkeletonTreeDataSource::ToTreeItem(_selected_joint);
        _skeleton_tree->ExpandParents(item);
        _skeleton_tree->SetSelectedItem(item, false);
        _skeleton_tree->ScrollItemIntoView(item);
    }

    void SkeletonAssetEditor::RefreshJointInfo()
    {
        if (_skeleton_asset == nullptr || _selected_joint == Joint::kInvalidJointIndex ||
            _selected_joint >= _skeleton_asset->GetSkeleton().JointNum())
        {
            if (_txt_selected_joint != nullptr) _txt_selected_joint->SetText("None");
            if (_txt_parent != nullptr) _txt_parent->SetText("-");
            if (_txt_children != nullptr) _txt_children->SetText("-");
            if (_txt_bind_position != nullptr) _txt_bind_position->SetText("-");
            if (_txt_bind_rotation != nullptr) _txt_bind_rotation->SetText("-");
            if (_txt_bind_scale != nullptr) _txt_bind_scale->SetText("-");
            return;
        }
        const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
        const Joint &joint = skeleton[_selected_joint];
        const String parent_name = joint._parent == Joint::kInvalidJointIndex ? "Root" :
            (joint._parent < skeleton.JointNum() ? skeleton[joint._parent]._name : "-");
        const Transform transform = skeleton.GetBindPose().GetLocalTransform(_selected_joint);
        if (_txt_selected_joint != nullptr) _txt_selected_joint->SetText(joint._name);
        if (_txt_parent != nullptr) _txt_parent->SetText(parent_name);
        if (_txt_children != nullptr) _txt_children->SetText(std::format("{}", joint._children.size()));
        if (_txt_bind_position != nullptr) _txt_bind_position->SetText(FormatVector(transform._position));
        if (_txt_bind_rotation != nullptr) _txt_bind_rotation->SetText(FormatQuaternion(transform._rotation));
        if (_txt_bind_scale != nullptr) _txt_bind_scale->SetText(FormatVector(transform._scale));
    }

    void SkeletonAssetEditor::RefreshPreview()
    {
        if (_skeleton_asset == nullptr)
        {
            _preview.SetMesh(nullptr);
            return;
        }
        _preview.SetPreviewBounds(BuildPreviewBounds());
        _preview.Render();
    }

    AABB SkeletonAssetEditor::BuildPreviewBounds() const
    {
        if (_skeleton_asset == nullptr || _skeleton_asset->GetSkeleton().JointNum() == 0u)
            return AABB(Vector3f(-0.5f), Vector3f(0.5f));

        const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
        const Pose &pose = skeleton.GetBindPose();
        AABB bounds = AABB::Infinity();
        bool has_joint = false;
        for (const Joint &joint : skeleton)
        {
            if (joint._self < pose.Size())
            {
                AABB::Encapsulate(bounds, pose.GetGlobalTransform(joint._self)._position);
                has_joint = true;
            }
        }
        if (!has_joint)
            return AABB(Vector3f(-0.5f), Vector3f(0.5f));
        const Vector3f size = bounds._max - bounds._min;
        const Vector3f padding(std::max(Magnitude(size) * 0.05f, 0.01f));
        bounds._min = bounds._min - padding;
        bounds._max = bounds._max + padding;
        return bounds;
    }

    bool SkeletonAssetEditor::BuildJointPositions(const Matrix4x4f &world_matrix,
                                                  Vector<Vector3f> &joint_positions) const
    {
        if (_skeleton_asset == nullptr)
            return false;
        const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
        const Pose &pose = skeleton.GetBindPose();
        joint_positions.resize(skeleton.JointNum());
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() || joint._self >= pose.Size())
                continue;
            joint_positions[joint._self] = TransformCoord(world_matrix, pose.GetGlobalTransform(joint._self)._position);
        }
        return !joint_positions.empty();
    }

    bool SkeletonAssetEditor::PickJoint(Vector2f local_position, u16 &joint_index) const
    {
        joint_index = Joint::kInvalidJointIndex;
        if (_skeleton_asset == nullptr || _skeleton_asset->GetSkeleton().JointNum() == 0u)
            return false;
        const AABB bounds = BuildPreviewBounds();
        const Vector3f center = (bounds._min + bounds._max) * 0.5f;
        const Vector3f size = bounds._max - bounds._min;
        Vector<Vector3f> joint_positions;
        if (!BuildJointPositions(MatrixTranslation(-center), joint_positions))
            return false;

        const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
        const f32 hit_radius = std::max(Magnitude(size) * 0.04f, 0.02f);
        f32 best_screen_distance_squared = std::numeric_limits<f32>::max();
        f32 best_depth = std::numeric_limits<f32>::max();
        const auto consider_hit = [&](u16 candidate, f32 distance_squared, f32 depth, f32 screen_radius)
        {
            const f32 hit_radius_pixels = std::max(screen_radius, 6.0f);
            if (distance_squared > hit_radius_pixels * hit_radius_pixels ||
                distance_squared > best_screen_distance_squared - Math::kFloatEpsilon ||
                (std::abs(distance_squared - best_screen_distance_squared) <= Math::kFloatEpsilon &&
                 depth >= best_depth))
                return;
            joint_index = candidate;
            best_screen_distance_squared = distance_squared;
            best_depth = depth;
        };

        Vector<Vector2f> screen_positions(skeleton.JointNum());
        Vector<f32> depths(skeleton.JointNum(), 0.0f);
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() ||
                !_preview.ProjectWorldPoint(joint_positions[joint._self], screen_positions[joint._self],
                                             depths[joint._self]))
                continue;
        }

        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() || depths[joint._self] <= Math::kFloatEpsilon)
                continue;
            const Vector2f difference = screen_positions[joint._self] - local_position;
            const f32 screen_radius = _preview.GetScreenRadius(hit_radius, depths[joint._self]);
            consider_hit(joint._self, DotProduct(difference, difference), depths[joint._self], screen_radius);
        }
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() || joint._parent == Joint::kInvalidJointIndex ||
                joint._parent >= joint_positions.size() || depths[joint._self] <= Math::kFloatEpsilon ||
                depths[joint._parent] <= Math::kFloatEpsilon)
                continue;
            f32 segment_factor = 0.0f;
            const f32 distance_squared = DistanceSquaredToScreenSegment(local_position,
                                                                          screen_positions[joint._parent],
                                                                          screen_positions[joint._self],
                                                                          segment_factor);
            const f32 depth = depths[joint._parent] +
                              (depths[joint._self] - depths[joint._parent]) * segment_factor;
            const f32 screen_radius = _preview.GetScreenRadius(hit_radius, depth);
            consider_hit(joint._self, distance_squared, depth, screen_radius);
        }
        return joint_index != Joint::kInvalidJointIndex;
    }

    void SkeletonAssetEditor::DrawSkeletonOverlay(Render::CommandBuffer *command_buffer,
                                                   const Matrix4x4f &world_matrix, const Vector3f &,
                                                   const Vector3f &extents)
    {
        if (command_buffer == nullptr || _skeleton_asset == nullptr || _skeleton_asset->GetSkeleton().JointNum() == 0u)
            return;
        auto material = Render::Material::s_standard_forward_lit.lock();
        if (material == nullptr)
            return;
        if (_skeleton_sphere == nullptr)
            _skeleton_sphere = Render::Mesh::s_sphere.lock();
        if (_skeleton_cone == nullptr)
            _skeleton_cone = Render::Mesh::s_cone.lock();
        if (!IsDrawableMesh(_skeleton_sphere.get()))
            _skeleton_sphere = ResourceMgr::Get().GetRef<Render::Mesh>(L"Meshs/src_res/sphere.alasset");
        if (!IsDrawableMesh(_skeleton_cone.get()))
            _skeleton_cone = ResourceMgr::Get().GetRef<Render::Mesh>(L"Meshs/src_res/cone.alasset");
        if (!IsDrawableMesh(_skeleton_sphere.get()))
            _skeleton_sphere = ResourceMgr::Get().Load<Render::Mesh>(L"Meshs/src_res/sphere.alasset");
        if (!IsDrawableMesh(_skeleton_cone.get()))
            _skeleton_cone = ResourceMgr::Get().Load<Render::Mesh>(L"Meshs/src_res/cone.alasset");
        if (!IsDrawableMesh(_skeleton_sphere.get()) || !IsDrawableMesh(_skeleton_cone.get()))
            return;

        if (_skeleton_joint_material == nullptr)
        {
            _skeleton_joint_material = material->CreateInstance();
            if (_skeleton_joint_material != nullptr)
                _skeleton_joint_material->SetVector("_AlbedoValue", Vector4f(0.18f, 0.72f, 1.0f, 1.0f));
        }
        if (_skeleton_bone_material == nullptr)
        {
            _skeleton_bone_material = material->CreateInstance();
            if (_skeleton_bone_material != nullptr)
                _skeleton_bone_material->SetVector("_AlbedoValue", Vector4f(0.08f, 0.34f, 0.62f, 1.0f));
        }
        if (_skeleton_selected_material == nullptr)
        {
            _skeleton_selected_material = material->CreateInstance();
            if (_skeleton_selected_material != nullptr)
                _skeleton_selected_material->SetVector("_AlbedoValue", Vector4f(1.0f, 0.72f, 0.08f, 1.0f));
        }
        if (_skeleton_joint_material == nullptr || _skeleton_bone_material == nullptr ||
            _skeleton_selected_material == nullptr)
            return;

        Vector<Vector3f> joint_positions;
        if (!BuildJointPositions(world_matrix, joint_positions))
            return;
        const Skeleton &skeleton = _skeleton_asset->GetSkeleton();
        const f32 skeleton_radius = std::max(Magnitude(extents), 0.001f) * 0.035f;
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size() || joint._parent == Joint::kInvalidJointIndex ||
                joint._parent >= joint_positions.size())
                continue;
            const Vector3f direction = joint_positions[joint._self] - joint_positions[joint._parent];
            if (Magnitude(direction) <= Math::kFloatEpsilon)
                continue;
            const bool selected = joint._self == _selected_joint;
            command_buffer->DrawMesh(_skeleton_cone.get(), selected ? _skeleton_selected_material.get() :
                                                                      _skeleton_bone_material.get(),
                                     MakeBoneMatrix(_skeleton_cone.get(), joint_positions[joint._parent], direction,
                                                   skeleton_radius));
        }
        for (const Joint &joint : skeleton)
        {
            if (joint._self >= joint_positions.size())
                continue;
            const bool selected = joint._self == _selected_joint;
            command_buffer->DrawMesh(_skeleton_sphere.get(), selected ? _skeleton_selected_material.get() :
                                                                        _skeleton_joint_material.get(),
                                     MakeJointMatrix(_skeleton_sphere.get(), joint_positions[joint._self],
                                                     skeleton_radius));
        }
    }

    void SkeletonAssetEditor::SelectJoint(u16 joint_index)
    {
        _selected_joint = joint_index;
        RefreshJointInfo();
        _preview.Render();
    }
}
