#include "Editors/MeshAssetEditor.h"

#include "Assets/Asset.h"
#include "Common/AssetEditorLayout.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Interface/IParser.h"
#include "Inspector/ReflectedPropertyPanel.h"
#include "Render/Mesh.h"
#include "UI/Basic.h"
#include "UI/Container.h"
#include "UI/UIFramework.h"

#include <format>

namespace Ailu::Editor
{
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
    }

    MeshAssetEditor::MeshAssetEditor() : AssetEditor("Mesh Editor", {1120.0f, 720.0f})
    {
        SetPosition({100.0f, 50.0f});
        _import_panel = MakeScope<ReflectedPropertyPanel>();

        auto *root = _content_root->AddChild<UI::VerticalBox>();
        root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *toolbar = root->AddChild<UI::HorizontalBox>();
        toolbar->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFixed)
            .Size({0.0f, 30.0f});
        BuildToolbar(toolbar);

        auto *main = root->AddChild<UI::SplitView>();
        main->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        main->SetRatio(0.70f);

        auto *center = main->AddChild<UI::Border>();
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
                _preview.BeginCameraDrag(event._mouse_position - rect.xy);
            else if (event._key_code == EKey::kRBUTTON)
                _preview.BeginCameraPan(event._mouse_position - rect.xy);
            event._is_handled = true;
        };
        _preview_image->OnMouseUp() += [this](UI::UIEvent &event)
        {
            if (event._key_code == EKey::kLBUTTON)
                _preview.EndCameraDrag();
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
            _preview.DragCamera(local_position);
            _preview.PanCamera(local_position);
            event._is_handled = true;
        };
        _preview_image->OnMouseScroll() += [this](UI::UIEvent &event)
        {
            _preview.ZoomCamera(event._scroll_delta);
            event._is_handled = true;
        };

        auto *right_border = main->AddChild<UI::Border>();
        right_border->_bg_color = kPanelColor;
        auto *right_scroll = right_border->AddChild<UI::ScrollView>();
        right_scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *right = right_scroll->AddChild<UI::VerticalBox>();
        right->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        right->SlotPadding() = UI::Padding(5.0f);
        BuildInfoPanel(right);
        BuildImportPanel(right);
    }

    MeshAssetEditor::~MeshAssetEditor() = default;

    void MeshAssetEditor::BuildToolbar(UI::HorizontalBox *toolbar)
    {
        AddAssetMenu(toolbar);
        auto add_button = [toolbar](const String &text, f32 width)
        {
            auto *button = toolbar->AddChild<UI::Button>(text);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size({width, 0.0f}).Margin({0.0f, 0.0f, 2.0f, 0.0f});
            return button;
        };
        auto *save = add_button("Save", 52.0f);
        save->OnMouseClick() += [this](UI::UIEvent &event) { Save(); event._is_handled = true; };
        _btn_reimport = add_button("Reimport", 72.0f);
        _btn_reimport->OnMouseClick() += [this](UI::UIEvent &event) { Reimport(); event._is_handled = true; };
        auto *focus = add_button("Focus", 52.0f);
        focus->OnMouseClick() += [this](UI::UIEvent &event) { FocusPreview(); event._is_handled = true; };
        _btn_grid = add_button("Grid: On", 66.0f);
        _btn_grid->OnMouseClick() += [this](UI::UIEvent &event) { ToggleGrid(); event._is_handled = true; };
        _btn_wireframe = add_button("Wire: Off", 72.0f);
        _btn_wireframe->OnMouseClick() += [this](UI::UIEvent &event) { ToggleWireframe(); event._is_handled = true; };
        auto *status = toolbar->AddChild<UI::Text>("Mesh preview");
        StyleText(status, kMutedTextColor);
        status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin({8.0f, 0.0f, 0.0f, 0.0f});
    }

    void MeshAssetEditor::BuildInfoPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Mesh");
        _info_root = panel->AddChild<UI::VerticalBox>();
        _info_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _txt_asset_name = AddInfoRow(_info_root, "Name");
        _txt_asset_path = AddInfoRow(_info_root, "Asset Path");
        _txt_source_path = AddInfoRow(_info_root, "Source");
        _txt_asset_guid = AddInfoRow(_info_root, "GUID");
        _txt_vertex_count = AddInfoRow(_info_root, "Vertices");
        _txt_triangle_count = AddInfoRow(_info_root, "Triangles");
        _txt_submesh_count = AddInfoRow(_info_root, "Submeshes");
        _txt_uv_channels = AddInfoRow(_info_root, "UV Channels");
        _txt_attributes = AddInfoRow(_info_root, "Attributes");
        _txt_bounds = AddInfoRow(_info_root, "Bounds");
        _txt_derived_data = AddInfoRow(_info_root, "Derived Data");
    }

    void MeshAssetEditor::BuildImportPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Import Settings");
        _import_root = panel->AddChild<UI::VerticalBox>();
        _import_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
    }

    void MeshAssetEditor::Update(f32 dt)
    {
        AssetEditor::Update(dt);
        if (_mesh == nullptr || _preview_image == nullptr)
            return;
        const Vector4f rect = _preview_image->GetArrangeRect();
        if (_preview.SetViewportSize({rect.z, rect.w}))
            _preview.Render();
        _preview_image->SetTexture(_preview.GetRenderTexture());
    }

    bool MeshAssetEditor::OnOpen()
    {
        _mesh = GetAssetObject<Render::Mesh>();
        if (_mesh == nullptr)
            return false;
        const auto *setting = dynamic_cast<const MeshImportSetting *>(
            ResourceMgr::Get().GetImportSetting(GetAsset()->_asset_path));
        SetEditorImportSetting(setting != nullptr ? *setting : MeshImportSetting::Default());
        _preview.SetMesh(_mesh);
        _preview.SetShowGrid(_show_grid);
        _preview.SetWireframe(_wireframe);
        RefreshAllUI();
        _preview.Render();
        return true;
    }

    void MeshAssetEditor::OnAssetSaved()
    {
        RefreshAllUI();
    }

    void MeshAssetEditor::OnAssetReloaded()
    {
        OnOpen();
    }

    void MeshAssetEditor::RefreshAllUI()
    {
        RefreshAssetInfo();
        RefreshImportSettings();
        if (_btn_grid != nullptr)
            _btn_grid->SetText(_show_grid ? "Grid: On" : "Grid: Off");
        if (_btn_wireframe != nullptr)
            _btn_wireframe->SetText(_wireframe ? "Wire: On" : "Wire: Off");
    }

    void MeshAssetEditor::RefreshAssetInfo()
    {
        if (_mesh == nullptr || GetAsset() == nullptr)
            return;
        u32 uv_channels = 0u;
        for (u8 channel = 0u; channel < Render::Mesh::kMaxUVChannels; ++channel)
            if (!_mesh->GetUVs(channel).empty())
                ++uv_channels;
        const AABB &bounds = _mesh->BoundBox().empty() ? AABB{} : _mesh->BoundBox()[0];
        if (_txt_asset_name) _txt_asset_name->SetText(_mesh->Name());
        if (_txt_asset_path) _txt_asset_path->SetText(ToChar(GetAsset()->_asset_path));
        if (_txt_source_path) _txt_source_path->SetText(ToChar(GetAsset()->_external_asset_path));
        if (_txt_asset_guid) _txt_asset_guid->SetText(GetAsset()->GetGuid().ToString());
        if (_txt_vertex_count) _txt_vertex_count->SetText(std::format("{}", _mesh->GetVertexCount()));
        if (_txt_triangle_count) _txt_triangle_count->SetText(std::format("{}", _mesh->GetTriangleCount()));
        if (_txt_submesh_count) _txt_submesh_count->SetText(std::format("{}", _mesh->SubmeshCount()));
        if (_txt_uv_channels) _txt_uv_channels->SetText(std::format("{}", uv_channels));
        if (_txt_attributes)
            _txt_attributes->SetText(std::format("N:{} T:{} C:{}", _mesh->GetNormals().empty() ? "No" : "Yes",
                                                  _mesh->GetTangents().empty() ? "No" : "Yes",
                                                  _mesh->GetColors().empty() ? "No" : "Yes"));
        if (_txt_bounds)
            _txt_bounds->SetText(std::format("({:.2f}, {:.2f}, {:.2f}) - ({:.2f}, {:.2f}, {:.2f})",
                                              bounds._min.x, bounds._min.y, bounds._min.z, bounds._max.x,
                                              bounds._max.y, bounds._max.z));
        if (_txt_derived_data)
            _txt_derived_data->SetText(_mesh->HasDerivedData() ? "Ready" : "Not Built");
    }

    void MeshAssetEditor::RefreshImportSettings()
    {
        if (_import_panel == nullptr || _import_root == nullptr)
            return;
        auto *setting = dynamic_cast<MeshImportSetting *>(GetEditorImportSetting());
        if (setting == nullptr)
            return;
        _import_root->ClearChildren();
        _import_panel->Build({MeshImportSetting::StaticType(), setting, _import_root, {}, {},
                              [this](const PropertyInfo &) { MarkDirty(); }});
    }

    void MeshAssetEditor::RefreshPreview()
    {
        _preview.SetMesh(_mesh);
        _preview.Render();
        if (_preview_image != nullptr)
            _preview_image->SetTexture(_preview.GetRenderTexture());
    }

    void MeshAssetEditor::Reimport()
    {
        if (GetAsset() == nullptr)
            return;
        if (IsDirty() && !Save())
            return;
        ResourceMgr::Get().ReimportAsset(GetAsset());
    }

    void MeshAssetEditor::FocusPreview()
    {
        _preview.ResetCamera();
        if (_preview_image != nullptr)
            _preview_image->SetTexture(_preview.GetRenderTexture());
    }

    void MeshAssetEditor::ToggleGrid()
    {
        _show_grid = !_show_grid;
        _preview.SetShowGrid(_show_grid);
        if (_btn_grid != nullptr)
            _btn_grid->SetText(_show_grid ? "Grid: On" : "Grid: Off");
        RefreshPreview();
    }

    void MeshAssetEditor::ToggleWireframe()
    {
        _wireframe = !_wireframe;
        _preview.SetWireframe(_wireframe);
        if (_btn_wireframe != nullptr)
            _btn_wireframe->SetText(_wireframe ? "Wire: On" : "Wire: Off");
        RefreshPreview();
    }

    void MeshAssetEditor::MarkDirty()
    {
        if (GetAsset() != nullptr && !GetAsset()->IsDirty())
            GetAsset()->MarkModified();
    }
}
