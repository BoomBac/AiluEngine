#include "Assets/AssetHandlers.h"
#include "Animation/Clip.h"
#include "Animation/TransformTrack.h"
#include "Assets/AssetDocument.h"
#include "Assets/ScriptAsset.h"
#include "Audio/AudioClip.h"
#include "Audio/AudioClipDocument.h"
#include "Framework/Common/FileManager.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"
#include "Framework/Core/Containers/List.h"
#include "Framework/Common/Assert.h"
#include "Framework/Interface/IParser.h"
#include "Framework/Math/Guid.h"
#include "Framework/Parser/AssetParser.h"
#include "Graph/GraphAsset.h"
#include "Input/InputActionAsset.h"
#include "Input/InputComposite.h"
#include "Objects/JsonArchive.h"
#include "Objects/Type.h"
#include "Render/2D/Sprite.h"
#include "Render/Camera.h"
#include "Render/Font.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace Ailu
{
using namespace Render;
using namespace SceneManagement;

namespace
{
    WString ResolveExternalAssetPath(const WString &asset_path, const WString &stored_external_path);
    WString MakeStoredExternalAssetPath(const WString &external_asset_path);
}

template<typename TDocument>
bool SaveAssetDocument(const WString &sys_path, TDocument &document);
template<typename TDocument>
bool LoadAssetDocument(const WString &sys_path, TDocument &document);
AssetDocumentHeader MakeAssetDocumentHeader(const Asset *asset);

// ============================================================
// ScriptAssetHandler
// ============================================================

const Type *ScriptAssetHandler::AssetType() const
{
    return ScriptAsset::StaticType();
}

Scope<Asset> ScriptAssetHandler::Load(const AssetLoadContext &context)
{
    ScriptAssetDocument document;
    if (!LoadAssetDocument(context._system_path, document))
        return nullptr;

    const WString source_file = ResolveExternalAssetPath(context._asset_path, ToWChar(document._file));
    const EAssetDomain asset_domain = context._resource_mgr->GetAssetPathDomain(context._asset_path);
    auto script_asset = MakeRef<ScriptAsset>(ToChar(ResourceMgr::NormalizeAssetPath(source_file, asset_domain)));
    script_asset->Name(document._header._asset_name);
    auto asset = MakeScope<Asset>(Guid(document._header._guid), ScriptAsset::StaticType(), context._asset_path);
    asset->_p_obj = script_asset;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool ScriptAssetHandler::Save(const AssetSaveContext &context)
{
    const ScriptAsset *script_asset = context._asset->As<ScriptAsset>();
    if (script_asset == nullptr)
        return false;

    ScriptAssetDocument document;
    document._header = MakeAssetDocumentHeader(context._asset);
    document._file = ToChar(PathUtils::GetFileName(ToWChar(script_asset->SourceFile()), true));
    return SaveAssetDocument(context._system_path, document);
}

// ============================================================
// Helper function templates (implementations)
// ============================================================

template<typename TDocument>
bool SaveAssetDocument(const WString &sys_path, TDocument &document)
{
    const Type *type = document.GetType();
    if (type == nullptr)
    {
        LOG_ERROR(L"Save asset document to {} failed, document type is nullptr", sys_path);
        return false;
    }
    JsonArchive ar;
    if constexpr (Serializable<TDocument>)
    {
        // 文档类自定义 Serialize 优先（例如 SceneAssetDocument 需显式写出场景格式版本）。
        document.Serialize(ar);
    }
    else
    {
        for (auto &prop : type->GetProperties())
        {
            prop.Serialize(&document, ar);
        }
    }
    ar.Save(sys_path);
    return true;
}

template<typename TDocument>
bool LoadAssetDocument(const WString &sys_path, TDocument &document)
{
    JsonArchive ar;
    ar.Load(sys_path);
    if (!ar.IsLoaded())
        return false;
    const Type *type = document.GetType();
    if (type == nullptr)
    {
        LOG_ERROR(L"Load asset document from {} failed, document type is nullptr", sys_path);
        return false;
    }
    if constexpr (Deserializable<TDocument>)
    {
        // 文档类自定义 Deserialize 优先（例如 SceneAssetDocument 需守卫缺失的场景格式版本字段）。
        document.Deserialize(ar);
    }
    else
    {
        for (auto &prop : type->GetProperties())
        {
            prop.Deserialize(&document, ar);
        }
    }
    return true;
}

AssetDocumentHeader MakeAssetDocumentHeader(const Asset *asset)
{
    AssetDocumentHeader header;
    header._format_version = kSerializedAssetDocumentVersion;
    header._guid = asset->GetGuid().ToString();
    header._asset_type = asset->_asset_type ? asset->_asset_type->FullName() : String{};
    header._asset_name = asset->Name();
    return header;
}

bool TryLoadAssetDocumentHeader(const WString &sys_path, AssetDocumentHeader &header)
{
    WString data;
    if (!FileManager::ReadFile(sys_path, data))
        return false;

    // Quick check: is it JSON?
    const WString trimmed = StringUtils::Trim(data);
    if (trimmed.empty() || trimmed.front() != L'{')
        return false;

    AssetHeaderProbeDocument probe;
    if (!LoadAssetDocument(sys_path, probe))
        return false;
    if (probe._header._format_version != kSerializedAssetDocumentVersion)
    {
        LOG_ERROR(L"Unsupported asset document version {} in {}", probe._header._format_version, sys_path);
        return false;
    }
    if (probe._header._guid.empty() || probe._header._asset_type.empty())
        return false;
    header = probe._header;
    return true;
}

// ============================================================
// Internal helpers shared across handlers
// ============================================================

namespace
{
    WString ResolveExternalAssetPath(const WString &asset_path, const WString &stored_external_path)
    {
        if (stored_external_path.empty() || PathUtils::IsSystemPath(stored_external_path))
            return stored_external_path;

        WString normalized_stored_path = PathUtils::FormatFilePath(stored_external_path);
        const bool has_directory = normalized_stored_path.find(L'/') != WString::npos;
        if (has_directory)
        {
            return ResourceMgr::NormalizeAssetPath(normalized_stored_path, ResourceMgr::GetAssetPathDomain(asset_path));
        }

        WString asset_dir = asset_path;
        for (auto &ch : asset_dir)
        {
            if (ch == L'\\')
                ch = L'/';
        }
        asset_dir = asset_dir.substr(0, asset_dir.find_last_of(L"/") + 1);
        return asset_dir + normalized_stored_path;
    }

    WString MakeStoredExternalAssetPath(const WString &external_asset_path)
    {
        if (external_asset_path.empty() || PathUtils::IsSystemPath(external_asset_path))
            return external_asset_path;
        return PathUtils::GetFileName(external_asset_path, true);
    }

    String GetLinkedAssetGuidString(Object *obj)
    {
        if (obj == nullptr)
            return {};
        const Guid &guid = ResourceMgr::Get().GetAssetGuid(obj);
        return guid == Guid::EmptyGuid() ? String{} : guid.ToString();
    }

    void FillMaterialGuidList(const Vector<Ref<Material>> &materials, Vector<String> &out_guids)
    {
        out_guids.clear();
        out_guids.reserve(materials.size());
        for (const auto &material : materials)
        {
            out_guids.emplace_back(GetLinkedAssetGuidString(material.get()));
        }
    }

    InputProcessorDocument ToInputProcessorDocument(const InputProcessor *processor)
    {
        InputProcessorDocument doc;
        if (const auto *typed_processor = dynamic_cast<const ScaleProcessor *>(processor))
        {
            const Vector3f &scale = typed_processor->GetScale();
            doc._type = "Scale";
            doc._params = Vector4f(scale.x, scale.y, scale.z, 0.0f);
        }
        else if (const auto *typed_processor = dynamic_cast<const InvertProcessor *>(processor))
        {
            doc._type = "Invert";
            doc._params = Vector4f(typed_processor->GetInvertX() ? 1.0f : 0.0f,
                                   typed_processor->GetInvertY() ? 1.0f : 0.0f,
                                   typed_processor->GetInvertZ() ? 1.0f : 0.0f, 0.0f);
        }
        else if (const auto *typed_processor = dynamic_cast<const StickDeadZoneProcessor *>(processor))
        {
            doc._type = "StickDeadZone";
            doc._params = Vector4f(typed_processor->GetMinDeadZone(), typed_processor->GetMaxDeadZone(), 0.0f, 0.0f);
        }
        else if (const auto *typed_processor = dynamic_cast<const AxisDeadZoneProcessor *>(processor))
        {
            doc._type = "AxisDeadZone";
            doc._params = Vector4f(typed_processor->GetDeadZone(), 0.0f, 0.0f, 0.0f);
        }
        else if (dynamic_cast<const NormalizeProcessor *>(processor) != nullptr)
        {
            doc._type = "Normalize";
        }
        else if (const auto *typed_processor = dynamic_cast<const ClampProcessor *>(processor))
        {
            doc._type = "Clamp";
            doc._params = Vector4f(typed_processor->GetMin(), typed_processor->GetMax(), 0.0f, 0.0f);
        }
        else if (const auto *typed_processor = dynamic_cast<const AxisToButtonProcessor *>(processor))
        {
            doc._type = "AxisToButton";
            doc._params = Vector4f(typed_processor->GetThreshold(), 0.0f, 0.0f, 0.0f);
        }
        return doc;
    }

    Scope<InputProcessor> FromInputProcessorDocument(const InputProcessorDocument &doc)
    {
        if (doc._type == "Scale")
            return MakeScope<ScaleProcessor>(Vector3f(doc._params.x, doc._params.y, doc._params.z));
        if (doc._type == "Invert")
            return MakeScope<InvertProcessor>(doc._params.x != 0.0f, doc._params.y != 0.0f, doc._params.z != 0.0f);
        if (doc._type == "StickDeadZone")
            return MakeScope<StickDeadZoneProcessor>(doc._params.x, doc._params.y);
        if (doc._type == "AxisDeadZone")
            return MakeScope<AxisDeadZoneProcessor>(doc._params.x);
        if (doc._type == "Normalize")
            return MakeScope<NormalizeProcessor>();
        if (doc._type == "Clamp")
            return MakeScope<ClampProcessor>(doc._params.x, doc._params.y);
        if (doc._type == "AxisToButton")
            return MakeScope<AxisToButtonProcessor>(doc._params.x);
        return nullptr;
    }

    InputInteractionDocument ToInputInteractionDocument(const InputInteraction *interaction)
    {
        InputInteractionDocument doc;
        if (dynamic_cast<const PressInteraction *>(interaction) != nullptr)
        {
            doc._type = "Press";
        }
        else if (dynamic_cast<const ReleaseInteraction *>(interaction) != nullptr)
        {
            doc._type = "Release";
        }
        else if (const auto *typed_interaction = dynamic_cast<const HoldInteraction *>(interaction))
        {
            doc._type = "Hold";
            doc._params = Vector4f(typed_interaction->GetDuration(), 0.0f, 0.0f, 0.0f);
        }
        else if (const auto *typed_interaction = dynamic_cast<const TapInteraction *>(interaction))
        {
            doc._type = "Tap";
            doc._params = Vector4f(typed_interaction->GetMaxDuration(), 0.0f, 0.0f, 0.0f);
        }
        return doc;
    }

    Scope<InputInteraction> FromInputInteractionDocument(const InputInteractionDocument &doc)
    {
        if (doc._type == "Press")
            return MakeScope<PressInteraction>();
        if (doc._type == "Release")
            return MakeScope<ReleaseInteraction>();
        if (doc._type == "Hold")
            return MakeScope<HoldInteraction>(doc._params.x);
        if (doc._type == "Tap")
            return MakeScope<TapInteraction>(doc._params.x);
        return nullptr;
    }

    InputBindingDocument ToInputBindingDocument(const InputBinding &binding)
    {
        InputBindingDocument doc;
        doc._name = binding._name;
        doc._control_path = binding._control_path;
        doc._groups = binding._groups;
        doc._is_composite = binding._is_composite;
        doc._is_part_of_composite = binding._is_part_of_composite;
        doc._composite_part_name = binding._composite_part_name;
        for (const auto &processor : binding._processors)
        {
            if (processor)
            {
                InputProcessorDocument processor_doc = ToInputProcessorDocument(processor.get());
                if (!processor_doc._type.empty())
                    doc._processors.emplace_back(std::move(processor_doc));
            }
        }
        for (const auto &interaction : binding._interactions)
        {
            if (interaction)
            {
                InputInteractionDocument interaction_doc = ToInputInteractionDocument(interaction.get());
                if (!interaction_doc._type.empty())
                    doc._interactions.emplace_back(std::move(interaction_doc));
            }
        }
        return doc;
    }

    InputBinding FromInputBindingDocument(const InputBindingDocument &doc)
    {
        InputBinding binding;
        binding._name = doc._name;
        binding._control_path = doc._control_path;
        binding._groups = doc._groups;
        binding._is_composite = doc._is_composite;
        binding._is_part_of_composite = doc._is_part_of_composite;
        binding._composite_part_name = doc._composite_part_name;
        for (const auto &processor_doc : doc._processors)
        {
            auto processor = FromInputProcessorDocument(processor_doc);
            if (processor)
                binding._processors.emplace_back(std::move(processor));
        }
        for (const auto &interaction_doc : doc._interactions)
        {
            auto interaction = FromInputInteractionDocument(interaction_doc);
            if (interaction)
                binding._interactions.emplace_back(std::move(interaction));
        }
        return binding;
    }

    void FillCompositeDocument(const InputAction &action, InputActionDocument &doc)
    {
        const auto &composite = action.GetComposite();
        if (!composite)
            return;

        if (const auto *axis_2d = dynamic_cast<const Axis2DCompositeBinding *>(composite.get()))
        {
            doc._composite_type = "Axis2D";
            doc._composite_params = Vector4f(axis_2d->_normalize ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            doc._composite_bindings = {ToInputBindingDocument(axis_2d->_up), ToInputBindingDocument(axis_2d->_down),
                                       ToInputBindingDocument(axis_2d->_left), ToInputBindingDocument(axis_2d->_right)};
        }
        else if (const auto *axis_1d = dynamic_cast<const Axis1DCompositeBinding *>(composite.get()))
        {
            doc._composite_type = "Axis1D";
            doc._composite_bindings = {ToInputBindingDocument(axis_1d->_positive), ToInputBindingDocument(axis_1d->_negative)};
        }
        else if (const auto *button_with_modifier = dynamic_cast<const ButtonWithModifierCompositeBinding *>(composite.get()))
        {
            doc._composite_type = "ButtonWithModifier";
            doc._composite_bindings = {ToInputBindingDocument(button_with_modifier->_modifier),
                                       ToInputBindingDocument(button_with_modifier->_button)};
        }
    }

    Scope<InputCompositeBinding> FromInputCompositeDocument(const InputActionDocument &doc)
    {
        if (doc._composite_type == "Axis2D" && doc._composite_bindings.size() >= 4u)
        {
            auto composite = MakeScope<Axis2DCompositeBinding>();
            composite->_normalize = doc._composite_params.x != 0.0f;
            composite->_up = FromInputBindingDocument(doc._composite_bindings[0]);
            composite->_down = FromInputBindingDocument(doc._composite_bindings[1]);
            composite->_left = FromInputBindingDocument(doc._composite_bindings[2]);
            composite->_right = FromInputBindingDocument(doc._composite_bindings[3]);
            return composite;
        }
        if (doc._composite_type == "Axis1D" && doc._composite_bindings.size() >= 2u)
        {
            auto composite = MakeScope<Axis1DCompositeBinding>();
            composite->_positive = FromInputBindingDocument(doc._composite_bindings[0]);
            composite->_negative = FromInputBindingDocument(doc._composite_bindings[1]);
            return composite;
        }
        if (doc._composite_type == "ButtonWithModifier" && doc._composite_bindings.size() >= 2u)
        {
            auto composite = MakeScope<ButtonWithModifierCompositeBinding>();
            composite->_modifier = FromInputBindingDocument(doc._composite_bindings[0]);
            composite->_button = FromInputBindingDocument(doc._composite_bindings[1]);
            return composite;
        }
        return nullptr;
    }

    InputActionDocument ToInputActionDocument(const InputAction &action)
    {
        InputActionDocument doc;
        doc._name = action.GetName();
        doc._id = action.GetId();
        doc._action_type = static_cast<u8>(action.GetActionType());
        doc._value_type = static_cast<u8>(action.GetValueType());
        doc._merge_strategy = static_cast<u8>(action.GetMergeStrategy());
        for (const auto &binding : action.GetBindings())
            doc._bindings.emplace_back(ToInputBindingDocument(binding));
        FillCompositeDocument(action, doc);
        return doc;
    }

    InputAction FromInputActionDocument(const InputActionDocument &doc)
    {
        InputAction action(doc._name);
        action.SetId(doc._id);
        action.SetActionType(static_cast<EInputActionType>(doc._action_type));
        action.SetValueType(static_cast<EInputValueType>(doc._value_type));
        action.SetMergeStrategy(static_cast<EBindingMergeStrategy>(doc._merge_strategy));
        for (const auto &binding_doc : doc._bindings)
            action.AddBinding(FromInputBindingDocument(binding_doc));
        auto composite = FromInputCompositeDocument(doc);
        if (composite)
            action.SetComposite(std::move(composite));
        return action;
    }

    InputActionMapDocument ToInputActionMapDocument(const InputActionMap &action_map)
    {
        InputActionMapDocument doc;
        doc._name = action_map.GetName();
        doc._id = action_map.GetId();
        for (const auto &action : action_map.GetActions())
            doc._actions.emplace_back(ToInputActionDocument(action));
        return doc;
    }

    InputActionMap FromInputActionMapDocument(const InputActionMapDocument &doc)
    {
        InputActionMap action_map(doc._name);
        action_map.SetId(doc._id);
        for (const auto &action_doc : doc._actions)
            action_map.AddAction(FromInputActionDocument(action_doc));
        return action_map;
    }

    InputContextDocument ToInputContextDocument(const InputContext &context)
    {
        InputContextDocument doc;
        doc._name = context.GetName();
        doc._priority = context.GetPriority();
        doc._consume_input = context.ConsumeInput();
        doc._block_lower_contexts = context.BlocksLowerContexts();
        doc._active = context.IsActive();
        for (const auto &action_map : context.GetActionMaps())
        {
            if (action_map)
                doc._action_map_names.emplace_back(action_map->GetName());
        }
        return doc;
    }

    InputContext FromInputContextDocument(const InputContextDocument &doc)
    {
        InputContext context(doc._name);
        context.SetPriority(doc._priority);
        context.SetConsumeInput(doc._consume_input);
        context.SetBlocksLowerContexts(doc._block_lower_contexts);
        context.SetActive(doc._active);
        for (const auto &action_map_name : doc._action_map_names)
            context.AddActionMap(MakeRef<InputActionMap>(action_map_name));
        return context;
    }
} // anonymous namespace


// ============================================================
// SpriteAssetHandler
// ============================================================

const Type *SpriteAssetHandler::AssetType() const
{
    return Sprite::StaticType();
}

Scope<Asset> SpriteAssetHandler::Load(const AssetLoadContext &context)
{
    SpriteAssetDocument document;
    if (!LoadAssetDocument(context._system_path, document))
        return nullptr;

    Ref<Texture2D> texture;
    if (document._texture != Guid::EmptyGuid())
        texture = context._resource_mgr->Load<Texture2D>(document._texture);

    auto sprite = MakeRef<Sprite>(document._header._asset_name);
    sprite->_texture = texture;
    sprite->_uv_rect = document._uv_rect;
    sprite->_pivot = document._pivot;
    sprite->_size = document._size;
    sprite->_border = document._border;

    auto asset = MakeScope<Asset>(Guid(document._header._guid),Sprite::StaticType(),context._asset_path);
    asset->_p_obj = sprite;

    return asset;
}

bool SpriteAssetHandler::Save(const AssetSaveContext &context)
{
    const Sprite *sprite = context._asset->As<Sprite>();
    if (sprite == nullptr)
        return false;

    SpriteAssetDocument document;
    document._header = MakeAssetDocumentHeader(context._asset);
    document._texture = Guid::EmptyGuid();
    bool has_valid_texture = sprite->_texture != nullptr;
    if (has_valid_texture)
    {
        auto guid = context._resource_mgr->GetAssetGuid(sprite->_texture.get());
        document._header._dependencies.push_back(AssetDependency{guid, EAssetDependencyType::kHard});
        document._texture = guid;
    }
    document._uv_rect = sprite->_uv_rect;
    document._pivot = sprite->_pivot;
    document._size = sprite->_size;
    document._border = sprite->_border;

    return SaveAssetDocument(context._system_path, document);
}

// ============================================================
// AudioClipAssetHandler
// ============================================================

const Type *AudioClipAssetHandler::AssetType() const
{
    return AudioClip::StaticType();
}

Scope<Asset> AudioClipAssetHandler::Load(const AssetLoadContext &context)
{
    AudioClipDocument doc;
    if (!LoadAssetDocument(context._system_path, doc))
        return nullptr;

    const WString source_file = ToWChar(doc._source_file);
    const WString resolved_file = ResolveExternalAssetPath(context._asset_path, source_file);
        auto clip = MakeRef<AudioClip>();
        clip->Name(doc._header._asset_name);
    clip->_load_mode = doc._load_mode;
    clip->_channel_mode = doc._channel_mode;
    clip->_force_mono = doc._force_mono;
    clip->_runtime_path = ToChar(context._resource_mgr->GetResSysPath(resolved_file));

    auto asset = MakeScope<Asset>(Guid(doc._header._guid), AudioClip::StaticType(), context._asset_path);
    asset->_external_asset_path = source_file;
    asset->_p_obj = clip;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool AudioClipAssetHandler::Save(const AssetSaveContext &context)
{
    const AudioClip *clip = context._asset->As<AudioClip>();
    if (clip == nullptr)
        return false;

    AudioClipDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._source_file = ToChar(MakeStoredExternalAssetPath(context._asset->_external_asset_path));
    doc._load_mode = clip->_load_mode;
    doc._channel_mode = clip->_channel_mode;
    doc._force_mono = clip->_force_mono;

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save audio clip to {} failed!", context._system_path);
        return false;
    }
    return true;
}

// ============================================================
// ShaderAssetHandler
// ============================================================

const Type *ShaderAssetHandler::AssetType() const
{
    return Shader::StaticType();
}

Scope<Asset> ShaderAssetHandler::Load(const AssetLoadContext &context)
{
    auto sys_path = context._system_path;
    WString data;
    if (!FileManager::ReadFile(sys_path, data))
        return nullptr;

    ShaderAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    auto file = ToWChar(doc._file);
    auto resolved_file = ResolveExternalAssetPath(context._asset_path, file);
    auto asset = MakeScope<Asset>(Guid(doc._header._guid),Shader::StaticType(),context._asset_path);
    asset->_external_asset_path = file;
    asset->_p_obj = Shader::Create(context._resource_mgr->GetResSysPath(resolved_file));
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool ShaderAssetHandler::Save(const AssetSaveContext &context)
{
    auto shader = context._asset->As<Shader>();
    auto [vs, ps] = shader->GetShaderEntry();
    ShaderAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._file = ToChar(MakeStoredExternalAssetPath(context._asset->_external_asset_path));
    doc._vs_entry = vs;
    doc._ps_entry = ps;
    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save shader to {} failed!", context._system_path);
        return false;
    }
    return true;
}

// ============================================================
// ComputeShaderAssetHandler
// ============================================================

const Type *ComputeShaderAssetHandler::AssetType() const
{
    return ComputeShader::StaticType();
}

Scope<Asset> ComputeShaderAssetHandler::Load(const AssetLoadContext &context)
{
    auto sys_path = context._system_path;
    WString data;
    if (!FileManager::ReadFile(sys_path, data))
        return nullptr;

    ComputeShaderAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    auto file = ToWChar(doc._file);
    auto resolved_file = ResolveExternalAssetPath(context._asset_path, file);
    auto asset = MakeScope<Asset>(Guid(doc._header._guid),ComputeShader::StaticType(),context._asset_path);
    asset->_external_asset_path = file;
    asset->_p_obj = ComputeShader::Create(context._resource_mgr->GetResSysPath(resolved_file));
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool ComputeShaderAssetHandler::Save(const AssetSaveContext &context)
{
    ComputeShaderAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._file = ToChar(MakeStoredExternalAssetPath(context._asset->_external_asset_path));
    doc._kernel = "Noname";

    // Check import settings for kernel name
    if (auto *setting = context._resource_mgr->GetImportSetting(context._asset_path))
    {
        if (auto *shader_setting = dynamic_cast<const ShaderImportSetting *>(setting))
        {
            if (!shader_setting->_cs_kernel.empty())
                doc._kernel = shader_setting->_cs_kernel;
        }
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save compute shader to {} failed!", context._system_path);
        return false;
    }
    return true;
}

// ============================================================
// TextureAssetHandler
// ============================================================

const Type *TextureAssetHandler::AssetType() const
{
    return Texture2D::StaticType();
}

Scope<Asset> TextureAssetHandler::Load(const AssetLoadContext &context)
{
    auto sys_path = context._system_path;
    WString data;
    auto setting = context._import_setting ? dynamic_cast<const TextureImportSetting *>(context._import_setting) : &TextureImportSetting::Default();
    auto tex_setting = setting ? *setting : TextureImportSetting::Default();

    if (!FileManager::ReadFile(sys_path, data))
        return nullptr;

    Texture2DAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;
    auto file = ToWChar(doc._file);
    auto resolved_file = ResolveExternalAssetPath(context._asset_path, file);
    auto json_setting = tex_setting;
    json_setting._is_sRGB = doc._is_srgb;

    auto tex = context._resource_mgr->LoadExternalTexture(resolved_file, json_setting);
    auto asset = MakeScope<Asset>(Guid(doc._header._guid),Texture2D::StaticType(),context._asset_path);
    asset->_external_asset_path = file;
    asset->_p_obj = tex;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);

    // Store import setting
    context._resource_mgr->SetImportSetting(context._asset_path, AL_NEW(TextureImportSetting, json_setting));
    return asset;
}

bool TextureAssetHandler::Save(const AssetSaveContext &context)
{
    Texture2DAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._file = ToChar(MakeStoredExternalAssetPath(context._asset->_external_asset_path));

    if (auto *setting = context._resource_mgr->GetImportSetting(context._asset_path))
    {
        if (auto *tex_setting = dynamic_cast<const TextureImportSetting *>(setting))
        {
            doc._is_srgb = tex_setting->_is_sRGB;
        }
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save texture2d to {} failed!", context._system_path);
        return false;
    }
    return true;
}

// ============================================================
// MaterialAssetHandler
// ============================================================

const Type *MaterialAssetHandler::AssetType() const
{
    return Material::StaticType();
}

Scope<Asset> MaterialAssetHandler::Load(const AssetLoadContext &context)
{
    WString sys_path = context._system_path;
    // Read file
    WString wdata;
    if (!FileManager::ReadFile(sys_path, wdata))
    {
        LOG_ERROR(L"Load material with path: {} failed!", sys_path);
        return nullptr;
    }

    MaterialAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    Shader *shader = context._resource_mgr->Load<Shader>(Guid(doc._shader_guid)).get();
    if (shader == nullptr)
    {
        LOG_ERROR(L"Load material with path: {} failed, shader {} is unavailable", sys_path, ToWChar(doc._shader_guid));
        return nullptr;
    }

    bool is_standard_mat = shader->Name() == "defered_standard_lit";
    Ref<Material> mat = is_standard_mat
        ? std::static_pointer_cast<Material>(MakeRef<StandardMaterial>(doc._header._asset_name))
        : MakeRef<Material>(shader, doc._header._asset_name);

    mat->SavedKeyworkds().clear();
    for (auto &kw : doc._keywords)
    {
        if (!kw.empty()) mat->SavedKeyworkds().insert(kw);
    }
    mat->Construct(true);

    for (auto &prop : doc._float_properties)
        mat->SetFloat(prop._name, prop._value);
    for (auto &prop : doc._vector_properties)
        mat->SetVector(prop._name, prop._value);
    for (auto &prop : doc._uint_properties)
        mat->SetInt(prop._name, prop._value);
    for (auto &prop : doc._int_vector_properties)
        mat->SetVector(prop._name, prop._value);

    for (auto &prop : doc._texture_properties)
    {
        if (prop._texture_guid.empty())
            continue;
        auto texture_asset_path = ResourceMgr::Get().GuidToAssetPath(Guid(prop._texture_guid));
        if (!texture_asset_path.empty())
        {
            mat->SetTexture(prop._name, context._resource_mgr->Load<Texture2D>(texture_asset_path).get());
        }
        else
        {
            LOG_WARNING("Load material: {}, property {} failed!", mat->Name(), prop._name);
        }
    }

    mat->GetUint("_MaterialID");
    if (is_standard_mat)
    {
        auto standard_mat = static_cast<StandardMaterial *>(mat.get());
        standard_mat->SurfaceType((ESurfaceType)standard_mat->GetUint("_surface"));
        standard_mat->MaterialID((EMaterialID)standard_mat->GetUint("_MaterialID"));
    }

    auto asset = MakeScope<Asset>(Guid(doc._header._guid),Material::StaticType(),context._asset_path);
    asset->_p_obj = mat;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool MaterialAssetHandler::Save(const AssetSaveContext &context)
{
    Material *mat = context._asset->As<Material>();
    if (mat == nullptr)
    {
        LOG_ERROR(L"Save material to {} failed, asset is not a Material", context._system_path);
        return false;
    }

    std::multimap<std::string, ShaderPropertyInfo *> props;
    for (auto &prop : mat->GetShaderProperty())
        props.insert(std::make_pair(prop->_value_name, prop));

    MaterialAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._shader_guid = context._resource_mgr->GetAssetGuid(mat->GetShader()).ToString();
    doc._keywords.assign(mat->SavedKeyworkds().begin(), mat->SavedKeyworkds().end());

    auto float_props = mat->GetAllFloatValue();
    auto vector_props = mat->GetAllVectorValue();
    auto int_vector_props = mat->GetAllIntVectorValue();
    auto uint_props = mat->GetAllUintValue();

    for (auto &[name, value] : uint_props)
    {
        AssetNamedUIntProperty entry;
        entry._name = name;
        entry._value = value;
        doc._uint_properties.push_back(entry);
    }
    for (auto &[name, value] : float_props)
    {
        AssetNamedFloatProperty entry;
        entry._name = name;
        entry._value = value;
        doc._float_properties.push_back(entry);
    }
    for (auto &[name, value] : vector_props)
    {
        AssetNamedVectorProperty entry;
        entry._name = name;
        entry._value = value;
        doc._vector_properties.push_back(entry);
    }
    for (auto &[name, value] : int_vector_props)
    {
        AssetNamedIntVectorProperty entry;
        entry._name = name;
        entry._value = value;
        doc._int_vector_properties.push_back(entry);
    }
    for (auto &[prop_name, prop] : props)
    {
        if (prop->_type == EShaderPropertyType::kTexture2D)
        {
            auto tex = reinterpret_cast<Texture *>(prop->_value_ptr);
            Guid tex_guid;
            if (tex)
            {
                Asset *linked_asset = context._resource_mgr->GetLinkedAsset(tex);
                if (linked_asset && linked_asset->_asset_type == Texture2D::StaticType())
                    tex_guid = linked_asset->GetGuid();
                else
                {
                    AL_ASSERT(true);
                    LOG_ERROR("Texture2D {} hasn't a linked asset or asset type error!", tex->Name());
                    tex_guid = Guid::EmptyGuid();
                }
            }
            else
            {
                tex_guid = Guid::EmptyGuid();
            }
            AssetTextureBinding entry;
            entry._name = prop->_value_name;
            entry._texture_guid = tex_guid == Guid::EmptyGuid() ? String{} : tex_guid.ToString();
            doc._texture_properties.push_back(entry);
        }
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save material to {} failed!", context._system_path);
        return false;
    }
    LOG_WARNING(L"Save material to {}", context._system_path);
    return true;
}

// ============================================================
// MeshAssetHandler (handles Mesh)
// ============================================================

const Type *MeshAssetHandler::AssetType() const
{
    return Mesh::StaticType();
}

static Scope<Asset> LoadMeshImpl(const AssetLoadContext &context)
{
    auto sys_path = context._system_path;
    WString data;
    List<Ref<AnimationClip>> clips;

    if (!FileManager::ReadFile(sys_path, data))
        return nullptr;

    MeshAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    auto file = ToWChar(doc._file);
    auto resolved_file = ResolveExternalAssetPath(context._asset_path, file);
    MeshImportSetting setting;
    setting._import_flag |= MeshImportSetting::kImportFlagMesh;
    setting._is_import_material = false;
    setting._mesh_name = doc._inner_file_name;
    setting._is_combine_mesh = doc._is_combine_mesh;

    auto &&mesh_list = std::move(context._resource_mgr->LoadExternalMesh(resolved_file, setting, clips));
    AL_ASSERT(mesh_list.size() != 0);

    bool is_sk_mesh = dynamic_cast<SkeletonMesh *>(mesh_list.front().get()) != nullptr;
    auto asset = MakeScope<Asset>(Guid(doc._header._guid),ComputeShader::StaticType(),context._asset_path);
    asset->_asset_type = is_sk_mesh ? (const Type*)SkeletonMesh::StaticType() : (const Type*)Mesh::StaticType();
    asset->_external_asset_path = file;
    asset->_p_obj = mesh_list.front();
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);

    context._resource_mgr->CreateAndRegisterEmbeddedMaterial(mesh_list.front().get());
    context._resource_mgr->SetImportSetting(context._asset_path, AL_NEW(MeshImportSetting, setting));
    return asset;
}

Scope<Asset> MeshAssetHandler::Load(const AssetLoadContext &context)
{
    return LoadMeshImpl(context);
}

bool MeshAssetHandler::Save(const AssetSaveContext &context)
{
    MeshAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._file = ToChar(MakeStoredExternalAssetPath(context._asset->_external_asset_path));
    doc._inner_file_name = context._asset->_p_obj->Name();

    if (auto *setting = context._resource_mgr->GetImportSetting(context._asset_path))
    {
        if (auto *mesh_setting = dynamic_cast<const MeshImportSetting *>(setting))
        {
            doc._is_combine_mesh = mesh_setting->_is_combine_mesh;
        }
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save mesh to {} failed!", context._system_path);
        return false;
    }
    return true;
}

// ============================================================
// SkeletonMeshAssetHandler
// ============================================================

const Type *SkeletonMeshAssetHandler::AssetType() const
{
    return SkeletonMesh::StaticType();
}

Scope<Asset> SkeletonMeshAssetHandler::Load(const AssetLoadContext &context)
{
    return LoadMeshImpl(context);
}

bool SkeletonMeshAssetHandler::Save(const AssetSaveContext &context)
{
    // Same as MeshAssetHandler::Save - the document is identical
    MeshAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._file = ToChar(MakeStoredExternalAssetPath(context._asset->_external_asset_path));
    doc._inner_file_name = context._asset->_p_obj->Name();

    if (auto *setting = context._resource_mgr->GetImportSetting(context._asset_path))
    {
        if (auto *mesh_setting = dynamic_cast<const MeshImportSetting *>(setting))
        {
            doc._is_combine_mesh = mesh_setting->_is_combine_mesh;
        }
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save skeleton mesh to {} failed!", context._system_path);
        return false;
    }
    return true;
}

// ============================================================
// SceneAssetHandler
// ============================================================

const Type *SceneAssetHandler::AssetType() const
{
    return Scene::StaticType();
}

Scope<Asset> SceneAssetHandler::Load(const AssetLoadContext &context)
{
    WString sys_path = context._system_path;
    WString data;
    if (!FileManager::ReadFile(sys_path, data))
        return nullptr;

    SceneAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    const String scene_name = !doc._header._asset_name.empty()
        ? doc._header._asset_name
        : ToChar(PathUtils::GetFileName(context._asset_path).c_str());

    Ref<Scene> loaded_scene = MakeRef<Scene>(scene_name);
    loaded_scene->SetAssetGuid(Guid(doc._header._guid));
    auto &reg = loaded_scene->GetRegister();

    const bool is_v2 = doc._scene_format_version >= SceneAssetDocument::kCurrentSceneFormatVersion;

    // First pass: create entities and build identity index.
    // 与 doc._entities 保持索引对齐，避免重复计算修复后的 GUID。
    Vector<ECS::Entity> created_entities;
    created_entities.reserve(doc._entities.size());

    // 修复后的 Guid -> 运行时 Entity；同时用于 V2 重复 GUID 检测与层级重建。
    HashMap<Guid, ECS::Entity, GuidHasher> guid_to_entity;
    guid_to_entity.reserve(doc._entities.size());
    // V1 旧场景：legacy entity id -> 新运行时 Entity。
    HashMap<ECS::Entity, ECS::Entity> old_to_new_entities;
    old_to_new_entities.reserve(doc._entities.size());

    for (const auto &entity_doc : doc._entities)
    {
        Guid guid = entity_doc._entity_guid;
        bool generated = false;
        if (guid.IsEmpty())
        {
            LOG_WARNING("Scene load: entity '{}' has empty guid, generating a new one", entity_doc._tag_component._name);
            generated = true;
        }
        else if (guid_to_entity.contains(guid))
        {
            LOG_ERROR("Scene load: duplicate guid {} for entity '{}', generating a new one", guid.ToString(), entity_doc._tag_component._name);
            generated = true;
        }

        ECS::Entity new_entity = loaded_scene->AddObject(entity_doc._tag_component._name, generated ? Guid::EmptyGuid() : guid);
        if (generated)
        {
            guid = loaded_scene->GetEntityGuid(new_entity);
        }
        if (auto *tag = reg.GetComponent<ECS::TagComponent>(new_entity))
        {
            tag->_layer_mask = entity_doc._tag_component._layer_mask;
        }

        created_entities.emplace_back(new_entity);
        guid_to_entity.emplace(guid, new_entity);

        if (!is_v2)
        {
            old_to_new_entities.emplace(static_cast<ECS::Entity>(entity_doc._entity_id), new_entity);
        }
    }

    // Helper to load material refs
    auto loadMaterialRefs = [&](const Vector<String> &mat_guids, Mesh *mesh, Vector<Ref<Material>> &materials)
    {
        materials.clear();
        materials.reserve(mat_guids.size());
        for (u32 index = 0u; index < mat_guids.size(); ++index)
        {
            Ref<Material> loaded_material = nullptr;
            const String &mat_guid_str = mat_guids[index];
            if (!mat_guid_str.empty())
            {
                const Guid mat_guid(mat_guid_str);
                context._resource_mgr->Load<Material>(mat_guid);
                loaded_material = context._resource_mgr->GetRef<Material>(mat_guid);
            }
            if (loaded_material == nullptr && mesh != nullptr)
            {
                loaded_material = context._resource_mgr->GetEmbeddedMaterial(mesh, static_cast<u16>(index));
            }
            if (loaded_material != nullptr)
            {
                materials.emplace_back(loaded_material);
            }
            else
            {
                LOG_WARNING("Load material slot {} failed for mesh {}", index, mesh != nullptr ? mesh->Name() : String("null"));
            }
        }
    };

    auto is_component_disabled = [](const SceneEntityDocument &entity_doc, StringView component_name)
    {
        return std::find(entity_doc._disabled_components.begin(), entity_doc._disabled_components.end(), component_name) !=
               entity_doc._disabled_components.end();
    };

    // Second pass: add all other components
    for (u32 doc_index = 0u; doc_index < doc._entities.size(); ++doc_index)
    {
        const auto &entity_doc = doc._entities[doc_index];
        const ECS::Entity entity = created_entities[doc_index];

        if (entity_doc._has_transform_component)
        {
            // CreateEntityInternal 已预置默认 TransformComponent，这里只覆盖文件中的本地变换。
            auto *component = reg.GetComponent<ECS::TransformComponent>(entity);
            if (component == nullptr)
                component = &reg.AddComponent<ECS::TransformComponent>(entity);
            component->_local_transform._position = entity_doc._transform_component._position;
            component->_local_transform._rotation = entity_doc._transform_component._rotation;
            component->_local_transform._scale = entity_doc._transform_component._scale;
        }
        if (entity_doc._has_script_component)
        {
            auto &component = reg.AddComponent<ECS::ScriptComponent>(entity);
            reg.SetComponentEnabled<ECS::ScriptComponent>(entity, !is_component_disabled(entity_doc, "ScriptComponent"));
            component._script_asset = entity_doc._script_component._script_asset;
            component._properties = entity_doc._script_component._properties;
        }
        if (entity_doc._has_static_mesh_component)
        {
            auto &component = reg.AddComponent<ECS::StaticMeshComponent>(entity);
            reg.SetComponentEnabled<ECS::StaticMeshComponent>(entity, !is_component_disabled(entity_doc, "StaticMeshComponent"));
            if (!entity_doc._static_mesh_component._mesh_guid.empty())
            {
                const Guid mesh_guid(entity_doc._static_mesh_component._mesh_guid);
                context._resource_mgr->Load<Mesh>(mesh_guid);
                component._p_mesh = context._resource_mgr->GetRef<Mesh>(mesh_guid);
                if (component._p_mesh != nullptr)
                {
                    component._transformed_aabbs.resize(component._p_mesh->SubmeshCount() + 1u);
                }
            }
            loadMaterialRefs(entity_doc._static_mesh_component._material_guids, component._p_mesh.get(), component._p_mats);
        }
        if (entity_doc._has_light_component)
        {
            auto &component = reg.AddComponent<ECS::LightComponent>(entity);
            reg.SetComponentEnabled<ECS::LightComponent>(entity, !is_component_disabled(entity_doc, "LightComponent"));
            if (!entity_doc._light_component._type.empty())
                if (const Enum *enum_type = StaticEnum<ECS::ELightType>())
                {
                    const i32 enum_index = enum_type->GetIndexByName(entity_doc._light_component._type);
                    if (enum_index != -1)
                        component._type = static_cast<ECS::ELightType>(enum_index);
                }
            component._light._light_color = entity_doc._light_component._light._light_color;
            component._light._light_param = entity_doc._light_component._light._light_param;
            component._light._is_two_side = entity_doc._light_component._light._is_two_side;
            component._shadow._is_cast_shadow = entity_doc._light_component._shadow._is_cast_shadow;
            component._shadow._constant_bias = entity_doc._light_component._shadow._constant_bias;
            component._shadow._slope_bias = entity_doc._light_component._shadow._slope_bias;
        }
        if (entity_doc._has_camera_component)
        {
            auto &component = reg.AddComponent<ECS::CCamera>(entity);
            reg.SetComponentEnabled<ECS::CCamera>(entity, !is_component_disabled(entity_doc, "CCamera"));
            if (!entity_doc._camera_component._type.empty())
                component._camera.Type(CameraTypeFromString(entity_doc._camera_component._type));
            component._camera.Aspect(entity_doc._camera_component._aspect);
            component._camera.Far(entity_doc._camera_component._far_clip);
            component._camera.Near(entity_doc._camera_component._near_clip);
            component._camera.FovH(entity_doc._camera_component._fov_h);
            component._camera.Size(entity_doc._camera_component._size);
            component._camera.MarkDirty();
            component._camera.RecalculateMatrix(true);
        }
        if (entity_doc._has_lightprobe_component)
        {
            auto &component = reg.AddComponent<ECS::CLightProbe>(entity);
            reg.SetComponentEnabled<ECS::CLightProbe>(entity, !is_component_disabled(entity_doc, "CLightProbe"));
            component._size = entity_doc._lightprobe_component._size;
            component._is_update_every_tick = entity_doc._lightprobe_component._is_update_every_tick;
            component._is_dirty = true;
        }
        if (entity_doc._has_rigidbody_component)
        {
            auto &component = reg.AddComponent<ECS::CRigidBody>(entity);
            reg.SetComponentEnabled<ECS::CRigidBody>(entity, !is_component_disabled(entity_doc, "CRigidBody"));
            component._mass = entity_doc._rigidbody_component._mass;
        }
        if (entity_doc._has_collider_component)
        {
            auto &component = reg.AddComponent<ECS::CCollider>(entity);
            reg.SetComponentEnabled<ECS::CCollider>(entity, !is_component_disabled(entity_doc, "CCollider"));
            if (!entity_doc._collider_component._type.empty())
                    if (const Enum *enum_type = StaticEnum<ECS::EColliderType>())
                    {
                        const i32 enum_index = enum_type->GetIndexByName(entity_doc._collider_component._type);
                        if (enum_index != -1)
                            component._type = static_cast<ECS::EColliderType>(enum_index);
                    }
            component._is_trigger = entity_doc._collider_component._is_trigger;
            component._center = entity_doc._collider_component._center;
            component._param = entity_doc._collider_component._param;
        }
        if (entity_doc._has_skeleton_mesh_component)
        {
            auto &component = reg.AddComponent<ECS::CSkeletonMesh>(entity);
            reg.SetComponentEnabled<ECS::CSkeletonMesh>(entity, !is_component_disabled(entity_doc, "CSkeletonMesh"));
            if (!entity_doc._skeleton_mesh_component._mesh_guid.empty())
            {
                const Guid mesh_guid(entity_doc._skeleton_mesh_component._mesh_guid);
                context._resource_mgr->Load<SkeletonMesh>(mesh_guid);
                component._p_mesh = context._resource_mgr->GetRef<SkeletonMesh>(mesh_guid);
                if (component._p_mesh != nullptr)
                {
                    component._transformed_aabbs.resize(component._p_mesh->SubmeshCount() + 1u);
                }
            }
            loadMaterialRefs(entity_doc._skeleton_mesh_component._material_guids, component._p_mesh.get(), component._p_mats);
            if (!entity_doc._skeleton_mesh_component._anim_clip_guid.empty())
            {
                const Guid clip_guid(entity_doc._skeleton_mesh_component._anim_clip_guid);
                context._resource_mgr->Load<AnimationClip>(clip_guid);
                component._anim_clip = context._resource_mgr->GetRef<AnimationClip>(clip_guid);
            }
        }
        if (entity_doc._has_vxgi_component)
        {
            auto &component = reg.AddComponent<ECS::CVXGI>(entity);
            reg.SetComponentEnabled<ECS::CVXGI>(entity, !is_component_disabled(entity_doc, "CVXGI"));
            component._grid_num = entity_doc._vxgi_component._grid_num;
            component._distance = entity_doc._vxgi_component._distance;
        }
        if (entity_doc._has_sprite_renderer_component)
        {
            auto &component = reg.AddComponent<ECS::SpriteRendererComponent>(entity);
            reg.SetComponentEnabled<ECS::SpriteRendererComponent>(entity,
                                                                  !is_component_disabled(entity_doc, "SpriteRendererComponent"));
            if (!entity_doc._sprite_renderer_component._sprite_guid.empty())
            {
                const Guid sprite_guid(entity_doc._sprite_renderer_component._sprite_guid);
                context._resource_mgr->Load<Sprite>(sprite_guid);
                component._sprite = context._resource_mgr->Get<Sprite>(sprite_guid);
            }
            if (!entity_doc._sprite_renderer_component._material_guid.empty())
            {
                const Guid mat_guid(entity_doc._sprite_renderer_component._material_guid);
                context._resource_mgr->Load<Material>(mat_guid);
                component._material = context._resource_mgr->GetRef<Material>(mat_guid);
            }
            const auto &c = entity_doc._sprite_renderer_component._color;
            component._color = Color(c.x, c.y, c.z, c.w);
            component._sorting_layer = static_cast<i16>(entity_doc._sprite_renderer_component._sorting_layer);
            component._order_in_layer = entity_doc._sprite_renderer_component._order_in_layer;
            component._blend_mode = static_cast<ESpriteBlendMode>(entity_doc._sprite_renderer_component._blend_mode);
            component._flip_x = entity_doc._sprite_renderer_component._flip_x;
            component._flip_y = entity_doc._sprite_renderer_component._flip_y;
            component._visible = entity_doc._sprite_renderer_component._visible;
        }
    }

    // Third pass: rebuild hierarchy through Scene API.
    // 统一收集每个实体的父 GUID 与兄弟顺序，按父分组稳定排序后调用 Reparent，不直接写 CHierarchy 链表字段。
    struct HierarchyIntent
    {
        ECS::Entity entity;
        Guid _parent_guid;
        u32 _sibling_index = 0u;
        const SceneHierarchyComponentDocument *_doc = nullptr;
    };

    Vector<HierarchyIntent> hierarchy_intents;
    if (is_v2)
    {
        for (u32 doc_index = 0u; doc_index < doc._entities.size(); ++doc_index)
        {
            const auto &entity_doc = doc._entities[doc_index];
            if (!entity_doc._has_hierarchy_component)
                continue;
            hierarchy_intents.push_back({created_entities[doc_index], entity_doc._hierarchy_component._parent_guid,
                                        entity_doc._hierarchy_component._sibling_index, &entity_doc._hierarchy_component});
        }
    }
    else
    {
        // V1 迁移：按 legacy _parent 解析父节点，并从 first_child -> next_sibling 链推导 sibling index。
        HashMap<u64, u32> legacy_id_to_doc_index;
        for (u32 i = 0u; i < doc._entities.size(); ++i)
            legacy_id_to_doc_index.emplace(doc._entities[i]._entity_id, i);

        HashMap<u64, u32> sibling_index_by_legacy_id;
        for (u32 doc_index = 0u; doc_index < doc._entities.size(); ++doc_index)
        {
            const auto &entity_doc = doc._entities[doc_index];
            if (!entity_doc._has_hierarchy_component)
                continue;
            u64 cursor = entity_doc._hierarchy_component._first_child;
            u32 idx = 0u;
            u32 guard = 0u;
            while (cursor != ECS::kInvalidEntity && guard <= doc._entities.size())
            {
                sibling_index_by_legacy_id[cursor] = idx++;
                const auto child_it = legacy_id_to_doc_index.find(cursor);
                if (child_it == legacy_id_to_doc_index.end())
                    break;
                const auto &child_doc = doc._entities[child_it->second];
                if (!child_doc._has_hierarchy_component)
                    break;
                cursor = child_doc._hierarchy_component._next_sibling;
                ++guard;
            }
        }

        for (u32 doc_index = 0u; doc_index < doc._entities.size(); ++doc_index)
        {
            const auto &entity_doc = doc._entities[doc_index];
            if (!entity_doc._has_hierarchy_component)
                continue;
            Guid parent_guid;
            if (entity_doc._hierarchy_component._parent != ECS::kInvalidEntity)
            {
                const auto parent_it = old_to_new_entities.find(static_cast<ECS::Entity>(entity_doc._hierarchy_component._parent));
                if (parent_it != old_to_new_entities.end())
                    parent_guid = loaded_scene->GetEntityGuid(parent_it->second);
            }
            u32 sibling_index = 0u;
            const auto sidx_it = sibling_index_by_legacy_id.find(entity_doc._entity_id);
            if (sidx_it != sibling_index_by_legacy_id.end())
                sibling_index = sidx_it->second;
            hierarchy_intents.push_back({created_entities[doc_index], parent_guid, sibling_index, &entity_doc._hierarchy_component});
        }
    }

    // 统一挂接：分组、稳定排序、逐节点 Reparent。
    HashMap<Guid, Vector<HierarchyIntent *>, GuidHasher> children_by_parent;
    for (auto &intent : hierarchy_intents)
        children_by_parent[intent._parent_guid].push_back(&intent);

    auto reparent_intent = [&](ECS::Entity child, ECS::Entity parent, const SceneHierarchyComponentDocument &hier_doc)
    {
        if (child == parent)
        {
            LOG_ERROR("Scene load: entity {} referenced as its own parent, keep as root", child);
            return;
        }
        if (!loaded_scene->Reparent(child, parent, false))
        {
            LOG_ERROR("Scene load: Reparent entity {} under {} failed (cycle or invalid), degraded to root", child, parent);
            return;
        }
        if (!hier_doc._inv_matrix_attach.empty())
        {
            if (auto *hier = reg.GetComponent<ECS::CHierarchy>(child))
                hier->_inv_matrix_attach.FromString(hier_doc._inv_matrix_attach);
        }
    };

    for (auto &[parent_guid, children] : children_by_parent)
    {
        if (parent_guid.IsEmpty())
            continue;// 根节点保持根
        const auto parent_it = guid_to_entity.find(parent_guid);
        if (parent_it == guid_to_entity.end())
        {
            LOG_ERROR("Scene load: parent guid {} not found, degrading its children to root", parent_guid.ToString());
            continue;
        }
        const ECS::Entity parent = parent_it->second;
        std::stable_sort(children.begin(), children.end(),
                         [](const HierarchyIntent *a, const HierarchyIntent *b) { return a->_sibling_index < b->_sibling_index; });
        for (const HierarchyIntent *intent : children)
            reparent_intent(intent->entity, parent, *intent->_doc);
    }

    for (u32 doc_index = 0u; doc_index < doc._entities.size(); ++doc_index)
    {
        const auto &entity_doc = doc._entities[doc_index];
        if (entity_doc._has_hierarchy_component && !entity_doc._hierarchy_component._enabled)
            loaded_scene->SetEntityEnabled(created_entities[doc_index], false);
    }

    // V1 迁移只在内存中升级，标记 Dirty 使下一次保存自动转为 V2。
    if (!is_v2)
        loaded_scene->MarkDirty();

    auto asset = MakeScope<Asset>(Guid(doc._header._guid),Scene::StaticType(),context._asset_path);
    asset->_asset_path = context._asset_path;
    asset->_asset_type = Scene::StaticType();
    asset->_p_obj = loaded_scene;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool SceneAssetHandler::Save(const AssetSaveContext &context)
{
    Scene *scene = context._asset->As<Scene>();
    auto sys_path = context._system_path;
    SceneAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._scene_format_version = SceneAssetDocument::kCurrentSceneFormatVersion;

    // 保存前校验身份索引；Release 发现缺失时修复，Debug 额外触发断言。
    const bool index_valid = scene->ValidateEntityGuidIndex();
    if (!index_valid)
    {
        LOG_ERROR("SceneAssetHandler::Save: entity guid index invalid for scene '{}', repairing", scene->Name());
        POW2_ASSERT(index_valid);
        scene->EnsureValidEntityIdentities();
    }

    const ECS::Register &reg = scene->GetRegister();
    const auto &tag_view = reg.View<ECS::TagComponent>();
    doc._entities.reserve(tag_view.size());

    // 预先按当前兄弟链顺序计算每个实体的 sibling index。
    HashMap<ECS::Entity, u32> entity_sibling_index;
    for (u64 index = 0u; index < tag_view.size(); ++index)
    {
        const ECS::Entity parent = reg.GetEntity<ECS::TagComponent>(index);
        const auto *parent_hier = reg.GetComponent<ECS::CHierarchy>(parent);
        if (parent_hier == nullptr || parent_hier->_first_child == ECS::kInvalidEntity)
            continue;
        ECS::Entity cursor = parent_hier->_first_child;
        u32 sibling_idx = 0u;
        u32 guard = 0u;
        while (cursor != ECS::kInvalidEntity && guard <= tag_view.size())
        {
            entity_sibling_index[cursor] = sibling_idx++;
            const auto *cursor_hier = reg.GetComponent<ECS::CHierarchy>(cursor);
            if (cursor_hier == nullptr)
                break;
            cursor = cursor_hier->_next_sibling;
            ++guard;
        }
    }

    for (u64 index = 0u; index < tag_view.size(); ++index)
    {
        ECS::Entity entity = reg.GetEntity<ECS::TagComponent>(index);
        const ECS::TagComponent &tag = tag_view[index];

        SceneEntityDocument entity_doc;
        entity_doc._entity_guid = scene->GetEntityGuid(entity);
        entity_doc._tag_component._name = tag._name;
        entity_doc._tag_component._layer_mask = tag._layer_mask;

        if (const auto *transform = reg.GetComponent<ECS::TransformComponent>(entity); transform != nullptr)
        {
            entity_doc._has_transform_component = true;
            entity_doc._transform_component._position = transform->_local_transform._position;
            entity_doc._transform_component._rotation = transform->_local_transform._rotation;
            entity_doc._transform_component._scale = transform->_local_transform._scale;
        }
        if (const auto *script = reg.GetComponent<ECS::ScriptComponent>(entity); script != nullptr)
        {
            entity_doc._has_script_component = true;
            if (!reg.IsComponentEnabled<ECS::ScriptComponent>(entity)) entity_doc._disabled_components.emplace_back("ScriptComponent");
            entity_doc._script_component._script_asset = script->_script_asset;
            entity_doc._script_component._properties = script->_properties;
        }
        if (const auto *static_mesh = reg.GetComponent<ECS::StaticMeshComponent>(entity); static_mesh != nullptr)
        {
            entity_doc._has_static_mesh_component = true;
            if (!reg.IsComponentEnabled<ECS::StaticMeshComponent>(entity)) entity_doc._disabled_components.emplace_back("StaticMeshComponent");
            entity_doc._static_mesh_component._mesh_guid = GetLinkedAssetGuidString(static_mesh->_p_mesh.get());
            FillMaterialGuidList(static_mesh->_p_mats, entity_doc._static_mesh_component._material_guids);
        }
        if (const auto *light = reg.GetComponent<ECS::LightComponent>(entity); light != nullptr)
        {
            entity_doc._has_light_component = true;
            if (!reg.IsComponentEnabled<ECS::LightComponent>(entity)) entity_doc._disabled_components.emplace_back("LightComponent");
            if (const Enum *enum_type = StaticEnum<ECS::ELightType>())
                entity_doc._light_component._type = enum_type->GetNameByEnum(light->_type);
            entity_doc._light_component._light._light_color = light->_light._light_color;
            entity_doc._light_component._light._light_param = light->_light._light_param;
            entity_doc._light_component._light._is_two_side = light->_light._is_two_side;
            entity_doc._light_component._shadow._is_cast_shadow = light->_shadow._is_cast_shadow;
            entity_doc._light_component._shadow._constant_bias = light->_shadow._constant_bias;
            entity_doc._light_component._shadow._slope_bias = light->_shadow._slope_bias;
        }
        if (const auto *hierarchy = reg.GetComponent<ECS::CHierarchy>(entity); hierarchy != nullptr)
        {
            entity_doc._has_hierarchy_component = true;
            entity_doc._hierarchy_component._enabled = hierarchy->_enabled;
            entity_doc._hierarchy_component._parent_guid = scene->GetEntityGuid(hierarchy->_parent);
            const auto sibling_it = entity_sibling_index.find(entity);
            entity_doc._hierarchy_component._sibling_index = sibling_it != entity_sibling_index.end() ? sibling_it->second : 0u;
            entity_doc._hierarchy_component._inv_matrix_attach = hierarchy->_inv_matrix_attach.ToString();
        }
        if (const auto *camera = reg.GetComponent<ECS::CCamera>(entity); camera != nullptr)
        {
            entity_doc._has_camera_component = true;
            if (!reg.IsComponentEnabled<ECS::CCamera>(entity)) entity_doc._disabled_components.emplace_back("CCamera");
            entity_doc._camera_component._type = CameraTypeToString(camera->_camera.Type());
            entity_doc._camera_component._aspect = camera->_camera.Aspect();
            entity_doc._camera_component._far_clip = camera->_camera.Far();
            entity_doc._camera_component._near_clip = camera->_camera.Near();
            entity_doc._camera_component._fov_h = camera->_camera.FovH();
            entity_doc._camera_component._size = camera->_camera.Size();
        }
        if (const auto *lightprobe = reg.GetComponent<ECS::CLightProbe>(entity); lightprobe != nullptr)
        {
            entity_doc._has_lightprobe_component = true;
            if (!reg.IsComponentEnabled<ECS::CLightProbe>(entity)) entity_doc._disabled_components.emplace_back("CLightProbe");
            entity_doc._lightprobe_component._size = lightprobe->_size;
            entity_doc._lightprobe_component._is_update_every_tick = lightprobe->_is_update_every_tick;
        }
        if (const auto *rigidbody = reg.GetComponent<ECS::CRigidBody>(entity); rigidbody != nullptr)
        {
            entity_doc._has_rigidbody_component = true;
            if (!reg.IsComponentEnabled<ECS::CRigidBody>(entity)) entity_doc._disabled_components.emplace_back("CRigidBody");
            entity_doc._rigidbody_component._mass = rigidbody->_mass;
        }
        if (const auto *collider = reg.GetComponent<ECS::CCollider>(entity); collider != nullptr)
        {
            entity_doc._has_collider_component = true;
            if (!reg.IsComponentEnabled<ECS::CCollider>(entity)) entity_doc._disabled_components.emplace_back("CCollider");
            if (const Enum *enum_type = StaticEnum<ECS::EColliderType>())
                entity_doc._collider_component._type = enum_type->GetNameByEnum(collider->_type);
            entity_doc._collider_component._is_trigger = collider->_is_trigger;
            entity_doc._collider_component._center = collider->_center;
            entity_doc._collider_component._param = collider->_param;
        }
        if (const auto *skeleton_mesh = reg.GetComponent<ECS::CSkeletonMesh>(entity); skeleton_mesh != nullptr)
        {
            entity_doc._has_skeleton_mesh_component = true;
            if (!reg.IsComponentEnabled<ECS::CSkeletonMesh>(entity)) entity_doc._disabled_components.emplace_back("CSkeletonMesh");
            entity_doc._skeleton_mesh_component._mesh_guid = GetLinkedAssetGuidString(skeleton_mesh->_p_mesh.get());
            FillMaterialGuidList(skeleton_mesh->_p_mats, entity_doc._skeleton_mesh_component._material_guids);
            entity_doc._skeleton_mesh_component._anim_clip_guid = GetLinkedAssetGuidString(skeleton_mesh->_anim_clip.get());
        }
        if (const auto *vxgi = reg.GetComponent<ECS::CVXGI>(entity); vxgi != nullptr)
        {
            entity_doc._has_vxgi_component = true;
            if (!reg.IsComponentEnabled<ECS::CVXGI>(entity)) entity_doc._disabled_components.emplace_back("CVXGI");
            entity_doc._vxgi_component._grid_num = vxgi->_grid_num;
            entity_doc._vxgi_component._distance = vxgi->_distance;
        }
        if (const auto *sprite = reg.GetComponent<ECS::SpriteRendererComponent>(entity); sprite != nullptr)
        {
            entity_doc._has_sprite_renderer_component = true;
            if (!reg.IsComponentEnabled<ECS::SpriteRendererComponent>(entity)) entity_doc._disabled_components.emplace_back("SpriteRendererComponent");
            entity_doc._sprite_renderer_component._sprite_guid = GetLinkedAssetGuidString(sprite->_sprite);
            entity_doc._sprite_renderer_component._material_guid = GetLinkedAssetGuidString(sprite->_material.get());
            entity_doc._sprite_renderer_component._color = Vector4f(sprite->_color.r, sprite->_color.g, sprite->_color.b, sprite->_color.a);
            entity_doc._sprite_renderer_component._sorting_layer = sprite->_sorting_layer;
            entity_doc._sprite_renderer_component._order_in_layer = sprite->_order_in_layer;
            entity_doc._sprite_renderer_component._blend_mode = static_cast<i32>(sprite->_blend_mode);
            entity_doc._sprite_renderer_component._flip_x = sprite->_flip_x;
            entity_doc._sprite_renderer_component._flip_y = sprite->_flip_y;
            entity_doc._sprite_renderer_component._visible = sprite->_visible;
        }
        doc._entities.emplace_back(std::move(entity_doc));
    }

    // 按 GUID 字符串排序，减少 ECS dense storage 调整导致的无意义文件 diff。
    std::sort(doc._entities.begin(), doc._entities.end(),
              [](const SceneEntityDocument &a, const SceneEntityDocument &b)
              { return a._entity_guid.ToString() < b._entity_guid.ToString(); });

    if (!SaveAssetDocument(sys_path, doc))
    {
        LOG_ERROR(L"Save scene failed to {}", sys_path);
        return false;
    }
    LOG_INFO(L"Save scene to {}", sys_path);
    return true;
}

// ============================================================
// AnimationClipAssetHandler
// ============================================================

const Type *AnimationClipAssetHandler::AssetType() const
{
    return AnimationClip::StaticType();
}

Scope<Asset> AnimationClipAssetHandler::Load(const AssetLoadContext &context)
{
    WString sys_path = context._system_path;
    WString data;
    if (!FileManager::ReadFile(sys_path, data))
        return nullptr;

    AnimationClipAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    Ref<AnimationClip> loaded_clip = MakeRef<AnimationClip>();
    loaded_clip->Name(!doc._clip_name.empty() ? doc._clip_name : doc._header._asset_name);
    loaded_clip->FrameCount(doc._frame_count);
    loaded_clip->Duration(doc._duration);
    loaded_clip->FrameRate(doc._frame_rate);
    const f32 frame_duration = doc._frame_duration > 0.0f ? doc._frame_duration
        : ((doc._frame_count > 0u && doc._duration > 0.0f) ? (doc._duration / static_cast<f32>(doc._frame_count)) : 0.0f);
    loaded_clip->FrameDuration(frame_duration);
    loaded_clip->IsLooping(doc._is_looping);
    loaded_clip->StartTime(0.0f);
    loaded_clip->EndTime(doc._duration);

    for (const auto &track_doc : doc._tracks)
    {
        auto &track = (*loaded_clip)[track_doc._joint_index];
        track.Resize(track_doc._frames.size());
        f32 cur_time = 0.0f;
        for (u32 frame_index = 0u; frame_index < track_doc._frames.size(); ++frame_index)
        {
            const auto &frame_doc = track_doc._frames[frame_index];
            auto pos_frame = TrackHelpers::FromVector(frame_doc._position);
            pos_frame._time = cur_time;
            auto rot_frame = TrackHelpers::FromQuaternion(frame_doc._rotation);
            rot_frame._time = cur_time;
            auto scale_frame = TrackHelpers::FromVector(frame_doc._scale);
            scale_frame._time = cur_time;
            track.GetPositionTrack()[frame_index] = pos_frame;
            track.GetRotationTrack()[frame_index] = rot_frame;
            track.GetScaleTrack()[frame_index] = scale_frame;
            cur_time += frame_duration;
        }
    }
    if (doc._duration <= 0.0f)
    {
        loaded_clip->RecalculateDuration();
    }
    
    auto asset = MakeScope<Asset>(Guid(doc._header._guid),AnimationClip::StaticType(),context._asset_path);
    asset->_p_obj = loaded_clip;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    AnimationClipLibrary::AddClip(loaded_clip);
    return asset;
}

bool AnimationClipAssetHandler::Save(const AssetSaveContext &context)
{
    AnimationClip *clip = context._asset->As<AnimationClip>();
    auto sys_path = context._system_path;
    AnimationClipAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._clip_name = clip->Name();
    doc._frame_count = clip->FrameCount();
    doc._duration = clip->Duration();
    doc._frame_rate = clip->FrameRate();
    doc._frame_duration = clip->FrameDuration();
    doc._is_looping = clip->IsLooping();
    doc._tracks.reserve(clip->Size());

    for (u32 index = 0u; index < clip->Size(); ++index)
    {
        const TransformTrack &track = clip->GetTrackAtIndex(index);
        const auto &pos_track = track.GetPositionTrack();
        const auto &rot_track = track.GetRotationTrack();
        const auto &scale_track = track.GetScaleTrack();

        AnimationClipTrackDocument track_doc;
        track_doc._joint_index = clip->GetIdAtIndex(index);
        track_doc._frames.reserve(pos_track.Size());
        for (u32 frame_index = 0u; frame_index < pos_track.Size(); ++frame_index)
        {
            AnimationClipFrameDocument frame_doc;
            frame_doc._position = TrackHelpers::ToVector(pos_track[frame_index]);
            frame_doc._rotation = TrackHelpers::ToQuaternion(rot_track[frame_index]);
            frame_doc._scale = TrackHelpers::ToVector(scale_track[frame_index]);
            track_doc._frames.emplace_back(std::move(frame_doc));
        }
        doc._tracks.emplace_back(std::move(track_doc));
    }

    if (!SaveAssetDocument(sys_path, doc))
    {
        LOG_ERROR(L"Save animclip failed to {}", sys_path);
        return false;
    }
    LOG_INFO(L"Save animclip to {}", sys_path);
    return true;
}

// ============================================================
// InputActionAssetHandler
// ============================================================

const Type *InputActionAssetHandler::AssetType() const
{
    return InputActionAsset::StaticType();
}

Scope<Asset> InputActionAssetHandler::Load(const AssetLoadContext &context)
{
    InputActionAssetDocument doc;
    if (!LoadAssetDocument(context._system_path, doc))
        return nullptr;

    auto input_asset = MakeRef<InputActionAsset>(doc._header._asset_name);
    for (const auto &action_map_doc : doc._action_maps)
        input_asset->AddActionMap(FromInputActionMapDocument(action_map_doc));
    for (const auto &context_doc : doc._contexts)
        input_asset->AddContext(FromInputContextDocument(context_doc));

    auto asset = MakeScope<Asset>(Guid(doc._header._guid), InputActionAsset::StaticType(), context._asset_path);
    asset->_p_obj = input_asset;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool InputActionAssetHandler::Save(const AssetSaveContext &context)
{
    const InputActionAsset *input_asset = context._asset->As<InputActionAsset>();
    if (input_asset == nullptr)
        return false;

    InputActionAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    for (const auto &action_map : input_asset->GetActionMaps())
        doc._action_maps.emplace_back(ToInputActionMapDocument(action_map));
    for (const auto &input_context : input_asset->GetContexts())
        doc._contexts.emplace_back(ToInputContextDocument(input_context));

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save input action asset failed to {}", context._system_path);
        return false;
    }
    LOG_INFO(L"Save input action asset to {}", context._system_path);
    return true;
}

// ============================================================
// GraphAssetHandler
// ============================================================

const Type *GraphAssetHandler::AssetType() const
{
    return GraphAsset::StaticType();
}

Scope<Asset> GraphAssetHandler::Load(const AssetLoadContext &context)
{
    GraphAssetDocument doc;
    if (!LoadAssetDocument(context._system_path, doc))
        return nullptr;

    auto graph = MakeRef<GraphAsset>(doc._header._asset_name);
    graph->SchemaType(doc._schema_type.empty() ? "FlowGraphSchema" : doc._schema_type);
    graph->MutableNodes() = doc._nodes;
    graph->MutableLinks() = doc._links;
    graph->MutableComments() = doc._comments;

    auto asset = MakeScope<Asset>(Guid(doc._header._guid), GraphAsset::StaticType(), context._asset_path);
    asset->_p_obj = graph;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool GraphAssetHandler::Save(const AssetSaveContext &context)
{
    const GraphAsset *graph = context._asset->As<GraphAsset>();
    if (graph == nullptr)
        return false;

    GraphAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._version = graph->Version();
    doc._schema_type = graph->SchemaType();
    doc._nodes = graph->Nodes();
    doc._links = graph->Links();
    doc._comments = graph->Comments();

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save graph asset failed to {}", context._system_path);
        return false;
    }
    LOG_INFO(L"Save graph asset to {}", context._system_path);
    return true;
}

} // namespace Ailu
