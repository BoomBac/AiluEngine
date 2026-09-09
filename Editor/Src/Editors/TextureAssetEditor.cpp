#include "Editors/TextureAssetEditor.h"

#include "Assets/Asset.h"
#include "Common/AssetEditorLayout.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Interface/IParser.h"
#include "Inspector/ReflectedPropertyPanel.h"
#include "Render/Texture.h"
#include "UI/Basic.h"
#include "UI/Container.h"

#include <algorithm>
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

        String FormatFormat(EALGFormat format)
        {
            const String &format_name = StaticEnum<EALGFormat>()->GetNameByEnum(format);
            constexpr StringView kFormatPrefix = "kALGFormat";
            if (format_name.starts_with(kFormatPrefix))
                return format_name.substr(kFormatPrefix.size());
            return format_name;
        }

        String FormatDimension(Render::ETextureDimension dimension)
        {
            switch (dimension)
            {
            case Render::ETextureDimension::kTex2D: return "2D";
            case Render::ETextureDimension::kTex3D: return "3D";
            case Render::ETextureDimension::kCube: return "Cube";
            case Render::ETextureDimension::kTex2DArray: return "2D Array";
            case Render::ETextureDimension::kCubeArray: return "Cube Array";
            default: return "Unknown";
            }
        }

        u64 EstimateMemory(const Render::Texture2D *texture)
        {
            if (texture == nullptr)
                return 0u;
            const u64 pixel_count = static_cast<u64>(texture->Width()) * texture->Height();
            const u64 mip_count = std::max<u16>(texture->MipmapLevel(), 1u);
            return pixel_count * 4u * mip_count;
        }
    }

    TextureAssetEditor::TextureAssetEditor() : AssetEditor("Texture Editor", {1040.0f, 680.0f})
    {
        SetPosition({120.0f, 60.0f});
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
        _preview = center->AddChild<TexturePreviewWidget>();
        _preview->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin({3.0f, 3.0f, 3.0f, 3.0f});

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

    TextureAssetEditor::~TextureAssetEditor() = default;

    void TextureAssetEditor::BuildToolbar(UI::HorizontalBox *toolbar)
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
        auto *reimport = add_button("Reimport", 72.0f);
        reimport->OnMouseClick() += [this](UI::UIEvent &event) { Reimport(); event._is_handled = true; };
        _btn_channel = add_button("RGBA", 56.0f);
        _btn_channel->OnMouseClick() += [this](UI::UIEvent &event)
        {
            SelectChannel((_channel + 1u) % 5u);
            event._is_handled = true;
        };
        _btn_mip = add_button("Mip: 0", 56.0f);
        _btn_mip->OnMouseClick() += [this](UI::UIEvent &event) { NextMip(); event._is_handled = true; };
        auto *fit = add_button("Fit", 42.0f);
        fit->OnMouseClick() += [this](UI::UIEvent &event)
        {
            if (_preview) _preview->Fit();
            event._is_handled = true;
        };
        auto *one_to_one = add_button("1:1", 42.0f);
        one_to_one->OnMouseClick() += [this](UI::UIEvent &event)
        {
            if (_preview) _preview->ResetZoom();
            event._is_handled = true;
        };
        auto *status = toolbar->AddChild<UI::Text>("Texture preview");
        StyleText(status, kMutedTextColor);
        status->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kFill)
            .Margin({8.0f, 0.0f, 0.0f, 0.0f});
    }

    void TextureAssetEditor::BuildInfoPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Texture Information");
        _info_root = panel->AddChild<UI::VerticalBox>();
        _info_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
        _txt_asset_name = AddInfoRow(_info_root, "Name");
        _txt_asset_path = AddInfoRow(_info_root, "Asset Path");
        _txt_source_path = AddInfoRow(_info_root, "Source");
        _txt_asset_guid = AddInfoRow(_info_root, "GUID");
        _txt_width = AddInfoRow(_info_root, "Width");
        _txt_height = AddInfoRow(_info_root, "Height");
        _txt_format = AddInfoRow(_info_root, "Pixel Format");
        _txt_mips = AddInfoRow(_info_root, "Mip Count");
        _txt_dimension = AddInfoRow(_info_root, "Dimension");
        _txt_memory = AddInfoRow(_info_root, "GPU Memory");
        _txt_readable = AddInfoRow(_info_root, "Readable");
        _txt_srgb = AddInfoRow(_info_root, "sRGB");
    }

    void TextureAssetEditor::BuildImportPanel(UI::VerticalBox *panel)
    {
        AssetEditorLayout::AddSectionTitle(panel, "Import Settings");
        _import_root = panel->AddChild<UI::VerticalBox>();
        _import_root->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
    }

    void TextureAssetEditor::Update(f32 dt)
    {
        AssetEditor::Update(dt);
        if (_preview != nullptr && _texture != nullptr)
            _preview->InvalidatePaint();
    }

    bool TextureAssetEditor::OnOpen()
    {
        _texture = GetAssetObject<Render::Texture2D>();
        if (_texture == nullptr)
            return false;
        const auto *setting = dynamic_cast<const TextureImportSetting *>(
            ResourceMgr::Get().GetImportSetting(GetAsset()->_asset_path));
        SetEditorImportSetting(setting != nullptr ? *setting : TextureImportSetting::Default());
        _preview->SetTexture(_texture);
        RefreshAllUI();
        return true;
    }

    void TextureAssetEditor::OnClose()
    {
        _texture = nullptr;
        if (_preview) _preview->SetTexture(nullptr);
    }

    void TextureAssetEditor::OnAssetSaved()
    {
        RefreshAllUI();
    }

    void TextureAssetEditor::OnAssetReloaded()
    {
        OnOpen();
    }

    void TextureAssetEditor::RefreshAllUI()
    {
        RefreshAssetInfo();
        RefreshImportSettings();
        SelectChannel(_channel);
        if (_btn_mip != nullptr)
            _btn_mip->SetText(std::format("Mip: {}", _preview != nullptr ? _preview->GetMip() : 0u));
    }

    void TextureAssetEditor::RefreshAssetInfo()
    {
        if (_texture == nullptr || GetAsset() == nullptr)
            return;
        if (_txt_asset_name) _txt_asset_name->SetText(_texture->Name());
        if (_txt_asset_path) _txt_asset_path->SetText(ToChar(GetAsset()->_asset_path));
        if (_txt_source_path) _txt_source_path->SetText(ToChar(GetAsset()->_external_asset_path));
        if (_txt_asset_guid) _txt_asset_guid->SetText(GetAsset()->GetGuid().ToString());
        if (_txt_width) _txt_width->SetText(std::format("{}", _texture->Width()));
        if (_txt_height) _txt_height->SetText(std::format("{}", _texture->Height()));
        if (_txt_format) _txt_format->SetText(FormatFormat(_texture->PixelFormat()));
        if (_txt_mips) _txt_mips->SetText(std::format("{}", _texture->MipmapLevel()));
        if (_txt_dimension) _txt_dimension->SetText(FormatDimension(_texture->Dimension()));
        if (_txt_memory) _txt_memory->SetText(std::format("{} bytes (estimate)", EstimateMemory(_texture)));
        if (_txt_readable) _txt_readable->SetText(_texture->Readble() ? "Yes" : "No");
        if (_txt_srgb) _txt_srgb->SetText(_texture->sRGB() ? "Yes" : "No");
    }

    void TextureAssetEditor::RefreshImportSettings()
    {
        if (_import_panel == nullptr || _import_root == nullptr)
            return;
        auto *setting = dynamic_cast<TextureImportSetting *>(GetEditorImportSetting());
        if (setting == nullptr)
            return;
        _import_root->ClearChildren();
        _import_panel->Build({TextureImportSetting::StaticType(), setting, _import_root, {}, {},
                              [this](const PropertyInfo &) { MarkDirty(); }});
    }

    void TextureAssetEditor::RefreshPreview()
    {
        if (_preview == nullptr)
            return;
        _preview->SetTexture(_texture);
        _preview->SetMip(_preview->GetMip());
        _preview->InvalidatePaint();
    }

    void TextureAssetEditor::SelectChannel(u32 channel)
    {
        _channel = channel % 5u;
        static const char *s_names[] = {"RGBA", "R", "G", "B", "A"};
        static const Vector4f s_masks[] = {
            Vector4f(1.0f, 1.0f, 1.0f, 1.0f), Vector4f(1.0f, 0.0f, 0.0f, 1.0f),
            Vector4f(0.0f, 1.0f, 0.0f, 1.0f), Vector4f(0.0f, 0.0f, 1.0f, 1.0f),
            Vector4f(1.0f, 1.0f, 1.0f, 1.0f)};
        if (_btn_channel) _btn_channel->SetText(s_names[_channel]);
        if (_preview) _preview->SetChannelMask(s_masks[_channel]);
    }

    void TextureAssetEditor::NextMip()
    {
        if (_preview == nullptr || _texture == nullptr)
            return;
        const u32 mip_count = std::max<u16>(_texture->MipmapLevel(), 1u);
        _preview->SetMip((_preview->GetMip() + 1u) % mip_count);
        if (_btn_mip) _btn_mip->SetText(std::format("Mip: {}", _preview->GetMip()));
        RefreshPreview();
    }

    void TextureAssetEditor::Reimport()
    {
        if (GetAsset() == nullptr)
            return;
        if (IsDirty() && !Save())
            return;
        ResourceMgr::Get().ReimportAsset(GetAsset());
    }

    void TextureAssetEditor::MarkDirty()
    {
        if (GetAsset() != nullptr && !GetAsset()->IsDirty())
            GetAsset()->MarkModified();
    }
}
