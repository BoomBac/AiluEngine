#include "Editors/MaterialAssetEditor.h"

#include "Assets/Asset.h"
#include "Common/AssetEditorLayout.h"
#include "Framework/Common/Input.h"
#include "Framework/Common/ResourceMgr.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Shader.h"
#include "UI/Basic.h"
#include "UI/ColorPicker.h"
#include "UI/Container.h"
#include "UI/ObjectAssetDropdown.h"
#include "UI/UIFramework.h"

#include <algorithm>
#include <format>
#include <memory>
#include <sstream>

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
            auto *row = AssetEditorLayout::AddPropertyRow(parent, label, 106.0f);
            auto *value = row->AddChild<UI::Text>("-");
            StyleText(value);
            value->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
            return value;
        }

        String FormatVector(const Vector4f &value)
        {
            return std::format("{:.3f}, {:.3f}, {:.3f}, {:.3f}", value.x, value.y, value.z, value.w);
        }

        void AddFloatProperty(UI::HorizontalBox *row, Render::Material *material, const String &name,
                              const std::function<void()> &on_changed)
        {
            auto *input = row->AddChild<UI::InputBlock>();
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto *property = material->GetShaderProperty(name);
            input->SetContent(property != nullptr ? std::format("{:.3f}", property->GetValue<f32>()) : "0.000");
            input->_on_content_changed += [material, name, on_changed](String content)
            {
                if (auto value = StringUtils::ParseFloat(content); value.has_value())
                {
                    material->SetFloat(name, value.value());
                    ResourceMgr::Get().MarkAssetDirty(material);
                    on_changed();
                }
            };
        }

        void UpdateColorButton(UI::Button *button, const Color &color)
        {
            if (button == nullptr)
                return;
            const Color srgb = color.ToSrgb();
            button->SetText(std::format("R {:.2f} G {:.2f} B {:.2f} A {:.2f}", srgb.r, srgb.g, srgb.b, color.a), false);
            UI::UIControlVisual visual;
            visual._background._type = UI::EUIBrushType::kColor;
            visual._background._tint = color;
            visual._border_color = Colors::kWhite;
            visual._border_width = Vector4f(1.0f);
            visual._corner_radius = Vector4f(3.0f);
            visual._content_color = srgb.r * 0.299f + srgb.g * 0.587f + srgb.b * 0.114f > 0.5f ?
                Colors::kBlack : Colors::kWhite;
            UI::UIButtonStyleOverride &style_override = button->GetStyleOverride();
            style_override.SetNormal(visual);
            style_override.SetHovered(visual);
            style_override.SetPressed(visual);
            style_override.SetFocused(visual);
            button->InvalidateStyle(UI::EStyleInvalidation::kPaintOnly);
        }

        void AddRangeProperty(UI::HorizontalBox *row, Render::Material *material, const String &name, Vector2f range,
                              const std::function<void()> &on_changed)
        {
            auto *property = material->GetShaderProperty(name);
            const f32 initial_value = property != nullptr ? property->GetValue<f32>() : range.x;
            auto *slider = row->AddChild<UI::Slider>();
            slider->GetSlotAs<UI::LinearSlot>().Margin({10.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(3.0f);
            slider->_range = range;
            slider->SetValue(initial_value, false);
            const String initial_content = std::format("{:.3f}", std::clamp(initial_value, range.x, range.y));
            auto *input = row->AddChild<UI::InputBlock>(initial_content);            input->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.0f);
            slider->_on_value_change += [material, name, input, on_changed](f32 value)
            {
                material->SetFloat(name, value);
                input->SetContent(std::format("{:.3f}", value), false);
                ResourceMgr::Get().MarkAssetDirty(material);
                on_changed();
            };
            input->_on_content_changed += [material, name, slider, range, on_changed](String content)
            {
                if (auto value = StringUtils::ParseFloat(content); value.has_value())
                {
                    const f32 clamped_value = std::clamp(value.value(), range.x, range.y);
                    material->SetFloat(name, clamped_value);
                    slider->SetValue(clamped_value, false);
                    ResourceMgr::Get().MarkAssetDirty(material);
                    on_changed();
                }
            };
        }

        bool ParseVector(const String &content, Vector4f &value)
        {
            String normalized = content;
            for (char &character : normalized)
                if (character == ',' || character == ';' || character == '(' || character == ')' || character == '[' || character == ']')
                    character = ' ';
            std::stringstream stream(normalized);
            return static_cast<bool>(stream >> value.x >> value.y >> value.z >> value.w);
        }
    }

    MaterialAssetEditor::MaterialAssetEditor() : AssetEditor("Material Editor", {1120.0f, 720.0f})
    {
        SetPosition({100.0f, 50.0f});
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
        auto *scroll = right_border->AddChild<UI::ScrollView>();
        scroll->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill);
        auto *right = scroll->AddChild<UI::VerticalBox>();
        right->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        right->SlotPadding() = UI::Padding(5.0f);
        BuildInfoPanel(right);
        AssetEditorLayout::AddSectionTitle(right, "Shader Properties");
        _property_root = right->AddChild<UI::VerticalBox>();
        _property_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
    }

    MaterialAssetEditor::~MaterialAssetEditor() = default;

    void MaterialAssetEditor::BuildToolbar(UI::HorizontalBox *toolbar)
    {
        auto add_button = [toolbar](const String &text, f32 width)
        {
            auto *button = toolbar->AddChild<UI::Button>(text);
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
                .Size({width, 0.0f}).Margin({0.0f, 0.0f, 2.0f, 0.0f});
            return button;
        };
        auto *save = add_button("Save", 52.0f);
        save->OnMouseClick() += [this](UI::UIEvent &event) { Save(); event._is_handled = true; };
        _btn_sphere = add_button("Sphere", 58.0f);
        _btn_sphere->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _preview_mesh_ref = Render::Mesh::s_sphere.lock();
            SetPreviewMesh(_preview_mesh_ref.get(), "Sphere");
            event._is_handled = true;
        };
        _btn_cube = add_button("Cube", 52.0f);
        _btn_cube->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _preview_mesh_ref = Render::Mesh::s_cube.lock();
            SetPreviewMesh(_preview_mesh_ref.get(), "Cube");
            event._is_handled = true;
        };
        _btn_plane = add_button("Plane", 52.0f);
        _btn_plane->OnMouseClick() += [this](UI::UIEvent &event)
        {
            _preview_mesh_ref = Render::Mesh::s_plane.lock();
            SetPreviewMesh(_preview_mesh_ref.get(), "Plane");
            event._is_handled = true;
        };
        auto *grid = add_button("Grid: On", 64.0f);
        grid->OnMouseClick() += [this, grid](UI::UIEvent &event)
        {
            _show_grid = !_show_grid;
            _preview.SetShowGrid(_show_grid);
            grid->SetText(_show_grid ? "Grid: On" : "Grid: Off");
            _preview.Render();
            event._is_handled = true;
        };
        auto *reset = add_button("Reset", 52.0f);
        reset->OnMouseClick() += [this](UI::UIEvent &event) { _preview.ResetCamera(); _preview.Render(); event._is_handled = true; };
        _shader_dropdown = toolbar->AddChild<UI::ObjectAssetDropdown>(Render::Shader::StaticType());
        _shader_dropdown->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFixed, UI::ESizePolicy::kFill)
            .Size({190.0f, 0.0f}).Margin({6.0f, 0.0f, 2.0f, 0.0f});
        _shader_dropdown->_on_object_asset_selected += [this](Asset *, Object *selected, const Guid &)
        {
            SetShader(dynamic_cast<Render::Shader *>(selected));
        };
        auto *status = toolbar->AddChild<UI::Text>("Material preview");
        StyleText(status, kMutedTextColor);
        status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin({8.0f, 0.0f, 0.0f, 0.0f});
    }

    void MaterialAssetEditor::BuildInfoPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Material");
        _txt_shader = AddInfoRow(panel, "Shader");
        _txt_surface = AddInfoRow(panel, "Surface");
        _txt_render_queue = AddInfoRow(panel, "Render Queue");
        _txt_keywords = AddInfoRow(panel, "Keywords");
        _txt_mesh = AddInfoRow(panel, "Preview Mesh");

        auto *surface_row = AssetEditorLayout::AddPropertyRow(panel, "Surface Type", 106.0f);
        _surface_dropdown = surface_row->AddChild<UI::Dropdown>(Vector<String>{"Opaque", "Transparent", "Alpha Test"});
        _surface_dropdown->_on_selected_changed += [this](i32 index)
        {
            if (_material != nullptr) { _material->SurfaceType(static_cast<Render::ESurfaceType>(index)); MarkDirty(); RefreshPreview(); }
        };
        auto *cull_row = AssetEditorLayout::AddPropertyRow(panel, "Cull Mode", 106.0f);
        _cull_dropdown = cull_row->AddChild<UI::Dropdown>(Vector<String>{"Off", "Front", "Back"});
        _cull_dropdown->_on_selected_changed += [this](i32 index)
        {
            if (_material != nullptr) { _material->SetCullMode(static_cast<Render::ECullMode>(index)); MarkDirty(); RefreshPreview(); }
        };
        auto *queue_row = AssetEditorLayout::AddPropertyRow(panel, "Render Queue", 106.0f);
        _render_queue_input = queue_row->AddChild<UI::InputBlock>();
        _render_queue_input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _render_queue_input->_on_content_changed += [this](String content)
        {
            if (_material == nullptr)
                return;
            try
            {
                const i32 value = std::stoi(content);
                _material->RenderQueue(static_cast<u16>(std::clamp(value, 0, 65535)));
                MarkDirty();
            }
            catch (const std::exception &) {}
        };
    }

    void MaterialAssetEditor::Update(f32 dt)
    {
        AssetEditor::Update(dt);
        if (_preview_image == nullptr || _preview_mesh == nullptr)
            return;
        const Vector4f rect = _preview_image->GetArrangeRect();
        if (_preview.SetViewportSize({rect.z, rect.w}) || _preview.IsRenderPending())
            _preview.Render();
        _preview_image->SetTexture(_preview.GetRenderTexture());
    }

    bool MaterialAssetEditor::OnOpen()
    {
        _material = GetAssetObject<Render::Material>();
        if (_material == nullptr)
            return false;
        _preview_mesh_ref = Render::Mesh::s_sphere.lock();
        if (_preview_mesh_ref == nullptr)
            _preview_mesh_ref = ResourceMgr::Get().Load<Render::Mesh>(L"Meshs/src_res/sphere.alasset");
        _preview_mesh = _preview_mesh_ref.get();
        _preview.SetMesh(_preview_mesh);
        _preview.SetMaterial(_material);
        _preview.SetShowGrid(_show_grid);
        RefreshAllUI();
        _preview.Render();
        return true;
    }

    void MaterialAssetEditor::OnClose()
    {
        _material = nullptr;
        _preview_mesh_ref.reset();
        _preview_mesh = nullptr;
        _preview.SetMesh(nullptr);
        _preview.SetMaterial(nullptr);
    }

    void MaterialAssetEditor::OnBeforeSave() {}
    void MaterialAssetEditor::OnAssetSaved() { RefreshAllUI(); }
    void MaterialAssetEditor::OnAssetReloaded() { OnOpen(); }

    void MaterialAssetEditor::RefreshAllUI()
    {
        if (_material == nullptr)
            return;
        RefreshInfo();
        RefreshProperties();
        if (_shader_dropdown != nullptr)
        {
            Guid shader_guid = _material->ShaderGuid();
            if (shader_guid.IsEmpty() && _material->GetShader() != nullptr)
                shader_guid = ResourceMgr::Get().GetAssetGuid(_material->GetShader());
            _shader_dropdown->SetSelectedGuid(shader_guid);
        }
    }

    void MaterialAssetEditor::RefreshProperties()
    {
        if (_property_root == nullptr || _material == nullptr)
            return;
        _property_root->ClearChildren();
        for (auto *property : _material->GetShaderProperty())
            if (property != nullptr)
                BuildProperty(_property_root, *property);
    }

    void MaterialAssetEditor::RefreshInfo()
    {
        if (_material == nullptr)
            return;
        if (_txt_shader) _txt_shader->SetText(_material->GetShader() != nullptr ? _material->GetShader()->Name() : "None");
        if (_txt_surface) _txt_surface->SetText(_material->SurfaceType() == Render::ESurfaceType::kOpaque ? "Opaque" :
                                                _material->SurfaceType() == Render::ESurfaceType::kTransparent ? "Transparent" : "Alpha Test");
        if (_txt_render_queue) _txt_render_queue->SetText(std::format("{}", _material->RenderQueue()));
        if (_surface_dropdown) _surface_dropdown->SetSelectedIndex(static_cast<i32>(_material->SurfaceType()), false);
        if (_cull_dropdown) _cull_dropdown->SetSelectedIndex(static_cast<i32>(_material->GetCullMode()), false);
        if (_render_queue_input) _render_queue_input->SetContent(std::format("{}", _material->RenderQueue()), false);
        if (_txt_keywords) _txt_keywords->SetText(std::format("{}", _material->SavedKeyworkds().size()));
        if (_txt_mesh) _txt_mesh->SetText(_preview_mesh_name);
    }

    void MaterialAssetEditor::RefreshPreview()
    {
        _preview.SetMaterial(_material);
        _preview.SetMesh(_preview_mesh);
        _preview.Render();
    }

    void MaterialAssetEditor::SetPreviewMesh(Render::Mesh *mesh, const String &name)
    {
        if (mesh == nullptr)
            return;
        _preview_mesh = mesh;
        _preview_mesh_name = name;
        RefreshInfo();
        RefreshPreview();
    }

    void MaterialAssetEditor::SetShader(Render::Shader *shader)
    {
        if (_material == nullptr || shader == nullptr || _material->GetShader() == shader)
            return;
        _material->ChangeShader(shader);
        _material->SetShaderGuid(ResourceMgr::Get().GetAssetGuid(shader));
        MarkDirty();
        RefreshAllUI();
        RefreshPreview();
    }

    void MaterialAssetEditor::MarkDirty()
    {
        if (GetAsset() != nullptr && !GetAsset()->IsDirty())
            GetAsset()->MarkModified();
    }

    void MaterialAssetEditor::BuildProperty(UI::VerticalBox *panel, Render::ShaderPropertyInfo &property)
    {
        auto *row = AssetEditorLayout::AddPropertyRow(panel, property._prop_name.empty() ? property._value_name : property._prop_name,
                                                       132.0f);
        if (_material == nullptr)
            return;
        if (property._type == Render::EShaderPropertyType::kRange)
        {
            AddRangeProperty(row, _material, property._value_name,
                             {property._default_value.x, property._default_value.y},
                             [this]() { MarkDirty(); RefreshPreview(); });
        }
        else if (property._type == Render::EShaderPropertyType::kFloat)
        {
            AddFloatProperty(row, _material, property._value_name, [this]() { MarkDirty(); RefreshPreview(); });
        }
        else if (property._type == Render::EShaderPropertyType::kBool)
        {
            auto *dropdown = row->AddChild<UI::Dropdown>(Vector<String>{"False", "True"});
            dropdown->SetSelectedIndex(property.GetValue<bool>() ? 1 : 0);
            const String name = property._value_name;
            dropdown->_on_selected_changed += [this, name](i32 index)
            {
                if (_material != nullptr) { _material->SetInt(name, index == 1 ? 1 : 0); MarkDirty(); }
            };
        }
        else if (property._type == Render::EShaderPropertyType::kVector)
        {
            auto *input = row->AddChild<UI::InputBlock>();
            input->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            input->SetContent(FormatVector(property.GetValue<Vector4f>()));
            const String name = property._value_name;
            input->_on_content_changed += [this, name](String content)
            {
                Vector4f value;
                if (_material != nullptr && ParseVector(content, value))
                {
                    _material->SetVector(name, value);
                    MarkDirty();
                    RefreshPreview();
                }
            };
        }
        else if (property._type == Render::EShaderPropertyType::kColor)
        {
            auto *button = row->AddChild<UI::Button>();
            button->Name(std::format("ColorButton_{}", property._value_name));
            button->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            const String name = property._value_name;
            UpdateColorButton(button, Color(property.GetValue<Vector4f>()));
            button->OnMouseClick() += [this, name, button](UI::UIEvent &event)
            {
                if (_material == nullptr)
                    return;
                auto *property = _material->GetShaderProperty(name);
                if (property == nullptr)
                    return;
                const Color current_color(property->GetValue<Vector4f>());
                auto color_picker = MakeRef<UI::ColorPicker>(current_color);
                color_picker->Name(std::format("ColorPicker_{}", name));
                color_picker->GetSlot()->Size({320.0f, 240.0f});
                color_picker->OnValueChanged() += [this, name, button](Color color)
                {
                    if (_material == nullptr)
                        return;
                    _material->SetVector(name, Vector4f(color));
                    MarkDirty();
                    RefreshPreview();
                    UpdateColorButton(button, color);
                };
                const Vector4f rect = event._current_target->GetArrangeRect();
                UI::UIManager::Get()->ShowPopupAt(rect.x, rect.y + rect.w, color_picker);
                event._is_handled = true;
            };
        }
        else if (property._type == Render::EShaderPropertyType::kTexture2D)
        {
            auto *dropdown = row->AddChild<UI::ObjectAssetDropdown>(Render::Texture2D::StaticType());
            auto *texture = reinterpret_cast<Render::Texture *>(property._value_ptr);
            const Guid texture_guid = _material->TextureGuid(property._value_name);
            if (!texture_guid.IsEmpty())
                dropdown->SetSelectedGuid(texture_guid);
            else if (texture != nullptr)
                dropdown->SetSelectedGuid(ResourceMgr::Get().GetAssetGuid(texture));
            const String name = property._value_name;
            dropdown->_on_object_asset_selected += [this, name](Asset *, Object *selected, const Guid &)
            {
                if (_material != nullptr) { _material->SetTexture(name, dynamic_cast<Render::Texture2D *>(selected)); MarkDirty(); RefreshPreview(); }
            };
        }
        else if (property._type == Render::EShaderPropertyType::kTexture3D)
        {
            auto *text = row->AddChild<UI::Text>("Texture3D");
            StyleText(text, kMutedTextColor);
        }
        else if (property._type == Render::EShaderPropertyType::kEnum)
        {
            auto *dropdown = row->AddChild<UI::Dropdown>(Vector<String>{"0", "1", "2", "3"});
            dropdown->SetSelectedIndex(static_cast<i32>(property.GetValue<u32>() & 3u));
            const String name = property._value_name;
            dropdown->_on_selected_changed += [this, name](i32 index)
            {
                if (_material != nullptr) { _material->SetInt(name, index); MarkDirty(); RefreshPreview(); }
            };
        }
    }
}
