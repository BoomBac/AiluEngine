#include "Assets/AssetHandlers.h"
#include "Animation/AnimationControllerAsset.h"
#include "Animation/BlendSpace.h"
#include "Animation/Clip.h"
#include "Animation/SkeletonAsset.h"
#include "Animation/TransformTrack.h"
#include "Assets/AssetDocument.h"
#include "Assets/AssetArtifact.h"
#include "Assets/AnimationClipArtifact.h"
#include "Assets/MeshArtifact.h"
#include "Assets/PrefabAsset.h"
#include "Assets/ScriptAsset.h"
#include "Assets/TextureArtifact.h"
#include "Assets/WidgetAsset.h"
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
#include "Framework/Parser/GltfParser.h"
#include "Graph/GraphAsset.h"
#include "Input/InputActionAsset.h"
#include "Input/InputComposite.h"
#include "Objects/JsonArchive.h"
#include "Objects/Type.h"
#include "Physics/2D/Physics2DComponents.h"
#include "Render/2D/Sprite.h"
#include "Render/2D/SpriteAtlas.h"
#include "Render/Camera.h"
#include "Render/Font.h"
#include "Render/GraphicsContext.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/Texture.h"
#include "Scene/EntitySerializer.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <limits>
#include <filesystem>

namespace Ailu
{
using namespace Render;
using namespace SceneManagement;

namespace
{
    WString ResolveExternalAssetPath(const WString &asset_path, const WString &stored_external_path);
    WString MakeStoredExternalAssetPath(const WString &external_asset_path);

    u64 ElapsedMicroseconds(const std::chrono::steady_clock::time_point start)
    {
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count());
    }

    u64 CalculateMeshDependencyHash(const WString &source_system_path)
    {
        const String extension = StringUtils::ToLower(fs::path(ToChar(source_system_path)).extension().string());
        if (extension != ".gltf")
            return 0u;

        u64 dependency_hash = 14695981039346656037ull;
        const fs::path source_path(source_system_path);
        for (const String &uri : GltfParser::CollectExternalDependencyUris(source_system_path))
        {
            const fs::path dependency_path = (source_path.parent_path() / fs::path(ToWChar(uri))).lexically_normal();
            SourceFingerprint fingerprint;
            if (!CalculateSourceFingerprint(dependency_path.wstring(), fingerprint))
                fingerprint = {};

            const u64 entry_hash = HashArtifactDependency(uri, fingerprint);
            for (u32 shift = 0u; shift < sizeof(entry_hash); ++shift)
            {
                dependency_hash ^= static_cast<u8>(entry_hash >> (shift * 8u));
                dependency_hash *= 1099511628211ull;
            }
        }
        return dependency_hash;
    }
}

template<typename TDocument>
bool SaveAssetDocument(const WString &sys_path, TDocument &document);
template<typename TDocument>
bool LoadAssetDocument(const WString &sys_path, TDocument &document);
AssetDocumentHeader MakeAssetDocumentHeader(const Asset *asset);

bool IAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    Object *target_object = target._p_obj.get();
    Object *source_object = const_cast<Object *>(source._p_obj.get());
    if (target_object == nullptr || source_object == nullptr || target_object->GetType() != source_object->GetType())
        return false;

    JsonArchive archive;
    for (const Type *type = source_object->GetType(); type != nullptr && type != Object::StaticType();
         type = type->BaseType())
    {
        for (const PropertyInfo &property : type->GetProperties())
            property.Serialize(source_object, archive);
    }
    for (const Type *type = target_object->GetType(); type != nullptr && type != Object::StaticType();
         type = type->BaseType())
    {
        for (const PropertyInfo &property : type->GetProperties())
            property.Deserialize(target_object, archive);
    }
    return true;
}

namespace
{
    void CopyMeshData(const Render::Mesh &source, Render::Mesh &target)
    {
        target.Clear();
        target.SetVertices(source.GetVertices());
        target.SetNormals(source.GetNormals());
        target.SetTangents(source.GetTangents());
        target.SetColors(source.GetColors());
        for (u8 channel = 0u; channel < 2u; ++channel)
            target.SetUVs(source.GetUVs(channel), channel);
        target.SetBounds(source.BoundBox());
        for (u16 submesh_index = 0u; submesh_index < source.SubmeshCount(); ++submesh_index)
            target.AddSubmesh(source.GetIndices(submesh_index));
        for (const auto &material : source.GetCacheMaterials())
            target.AddCacheMaterial(material);
    }
}

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
    return ar.Save(sys_path);
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
    sprite->_texture.Set(document._texture, std::move(texture));
    sprite->_uv_rect = document._uv_rect;
    sprite->_pivot = document._pivot;
    // Sprite assets written before the uniform-size model stored the final width and height.
    // Keep those assets loadable by using the old height as the new uniform base size.
    sprite->_size = std::max(document._size.y, 0.0001f);
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
    document._texture = sprite->_texture.GetGuid();
    if (document._texture.IsEmpty() && sprite->_texture != nullptr)
        document._texture = context._resource_mgr->GetAssetGuid(sprite->_texture.get());
    if (!document._texture.IsEmpty())
        document._header._dependencies.push_back(AssetDependency{document._texture, EAssetDependencyType::kHard});
    document._uv_rect = sprite->_uv_rect;
    document._pivot = sprite->_pivot;
    document._size = sprite->GetRenderSize();
    document._border = sprite->_border;

    return SaveAssetDocument(context._system_path, document);
}

// ============================================================
// SpriteAtlasAssetHandler
// ============================================================

const Type *SpriteAtlasAssetHandler::AssetType() const
{
    return SpriteAtlas::StaticType();
}

Scope<Asset> SpriteAtlasAssetHandler::Load(const AssetLoadContext &context)
{
    SpriteAtlasAssetDocument document;
    if (!LoadAssetDocument(context._system_path, document))
        return nullptr;

    Ref<Texture2D> texture;
    if (document._texture != Guid::EmptyGuid())
        texture = context._resource_mgr->Load<Texture2D>(document._texture);

    auto atlas = MakeRef<SpriteAtlas>(document._header._asset_name);
    atlas->SetTexture(document._texture, std::move(texture));
    auto asset = MakeScope<Asset>(Guid(document._header._guid), SpriteAtlas::StaticType(), context._asset_path);
    asset->_p_obj = atlas;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);

    for (const auto &entry : document._sprites)
    {
        Guid sprite_guid = entry._guid;
        if (sprite_guid.IsEmpty())
            sprite_guid = Guid::Generate();

        auto sprite = MakeRef<Sprite>(entry._name);
        sprite->_texture.Set(document._texture, texture);
        sprite->_uv_rect = entry._uv_rect;
        sprite->_pivot = entry._pivot;
        sprite->_size = std::max(entry._size, 0.0001f);
        sprite->_border = entry._border;
        atlas->Sprites().push_back(sprite);
        context._resource_mgr->RegisterSubAsset(asset.get(), sprite_guid, sprite, entry._name);
    }

    return asset;
}

bool SpriteAtlasAssetHandler::Save(const AssetSaveContext &context)
{
    const auto *atlas = context._asset->As<SpriteAtlas>();
    if (atlas == nullptr)
        return false;

    SpriteAtlasAssetDocument document;
    document._header = MakeAssetDocumentHeader(context._asset);
    document._texture = atlas->TextureRef().GetGuid();
    if (document._texture.IsEmpty() && atlas->Texture() != nullptr)
        document._texture = context._resource_mgr->GetAssetGuid(atlas->Texture().get());
    if (!document._texture.IsEmpty())
        document._header._dependencies.emplace_back(document._texture, EAssetDependencyType::kHard);

    for (const auto &sprite : atlas->Sprites())
    {
        if (sprite == nullptr)
            continue;
        SpriteAtlasEntryDocument entry;
        entry._guid = context._resource_mgr->GetAssetGuid(sprite.get());
        if (entry._guid.IsEmpty())
        {
            entry._guid = Guid::Generate();
            context._resource_mgr->RegisterSubAsset(const_cast<Asset *>(context._asset), entry._guid, sprite,
                                                    sprite->Name());
        }
        entry._name = sprite->Name();
        entry._uv_rect = sprite->_uv_rect;
        entry._pivot = sprite->_pivot;
        entry._size = sprite->_size;
        entry._border = sprite->_border;
        document._sprites.emplace_back(entry);
    }

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

bool SpriteAtlasAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    auto *target_atlas = target.As<SpriteAtlas>();
    const auto *source_atlas = source.As<SpriteAtlas>();
    if (target_atlas == nullptr || source_atlas == nullptr)
        return false;

    target_atlas->SetTexture(source_atlas->TextureRef().GetGuid(), source_atlas->TextureRef().Get());
    auto &target_sprites = target_atlas->Sprites();
    const auto &source_sprites = source_atlas->Sprites();
    target_sprites.resize(source_sprites.size());
    for (u32 i = 0u; i < source_sprites.size(); ++i)
    {
        if (target_sprites[i] == nullptr)
            target_sprites[i] = MakeRef<Sprite>();
        const Sprite *source_sprite = source_sprites[i].get();
        Sprite *target_sprite = target_sprites[i].get();
        if (source_sprite == nullptr || target_sprite == nullptr)
            continue;
        target_sprite->Name(source_sprite->Name());
        target_sprite->_texture = source_sprite->_texture;
        target_sprite->_uv_rect = source_sprite->_uv_rect;
        target_sprite->_pivot = source_sprite->_pivot;
        target_sprite->_size = source_sprite->_size;
        target_sprite->_border = source_sprite->_border;
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
    Texture2DAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;
    auto file = ToWChar(doc._file);
    auto resolved_file = ResolveExternalAssetPath(context._asset_path, file);
    auto json_setting = doc._import_setting;

    const WString source_system_path = context._resource_mgr->GetResSysPath(resolved_file);
    AssetArtifactKey artifact_key;
    SourceFingerprint source_fingerprint;
    artifact_key._importer_version = 1u;
    artifact_key._artifact_version = kTextureArtifactVersion;
    artifact_key._import_setting_hash = HashTextureImportSetting(json_setting);

    Ref<Texture2D> tex;
    bool loaded_from_artifact = false;
    u64 artifact_read_us = 0u;
    u64 native_import_us = 0u;
    u64 runtime_create_us = 0u;
    u64 gpu_upload_us = 0u;
    u64 artifact_write_us = 0u;
    u64 artifact_size = 0u;
    WString artifact_path;
    Vector<u8> artifact_data;
    const bool has_source_fingerprint = CalculateSourceFingerprint(source_system_path, source_fingerprint);
    if (has_source_fingerprint)
    {
        artifact_key._source_hash = source_fingerprint._content_hash;
        if (context._derived_data_cache != nullptr)
            artifact_path = context._derived_data_cache->GetArtifactPath(Guid(doc._header._guid), artifact_key);
        const auto artifact_read_start = std::chrono::steady_clock::now();
        const bool artifact_loaded = context._derived_data_cache != nullptr &&
                                     context._derived_data_cache->TryLoad(Guid(doc._header._guid), artifact_key,
                                                                          artifact_data);
        artifact_read_us = ElapsedMicroseconds(artifact_read_start);
        if (artifact_loaded)
        {
            artifact_size = artifact_data.size();
            const auto runtime_create_start = std::chrono::steady_clock::now();
            TextureArtifact artifact;
            if (DeserializeTextureArtifact(artifact_data, artifact_key, artifact))
            {
                tex = CreateTextureFromArtifact(artifact);
                if (tex != nullptr)
                {
                    loaded_from_artifact = true;
                    LOG_INFO(L"Texture artifact cache hit: {}", source_system_path);
                }
            }
            runtime_create_us = ElapsedMicroseconds(runtime_create_start);
            artifact_data.clear();
        }
    }

    if (tex == nullptr)
    {
        LOG_INFO(L"Texture artifact cache miss: {}", source_system_path);
        const auto native_import_start = std::chrono::steady_clock::now();
        tex = context._resource_mgr->LoadExternalTexture(resolved_file, json_setting);
        native_import_us = ElapsedMicroseconds(native_import_start);
        if (tex != nullptr && has_source_fingerprint && context._derived_data_cache != nullptr)
        {
            const auto artifact_write_start = std::chrono::steady_clock::now();
            TextureArtifact artifact;
            Vector<u8> serialized_artifact;
            if (BuildTextureArtifact(*tex, artifact) &&
                SerializeTextureArtifact(artifact, artifact_key, serialized_artifact))
            {
                artifact_size = serialized_artifact.size();
                context._derived_data_cache->Store(Guid(doc._header._guid), artifact_key, serialized_artifact);
            }
            artifact_write_us = ElapsedMicroseconds(artifact_write_start);
        }
    }
    if (tex == nullptr)
        return nullptr;
    const String texture_name = !doc._header._asset_name.empty()
        ? doc._header._asset_name
        : ToChar(PathUtils::GetFileName(context._asset_path).c_str());
    tex->Name(texture_name);
    if (loaded_from_artifact)
    {
        const auto gpu_upload_start = std::chrono::steady_clock::now();
        tex->Apply();
        gpu_upload_us = ElapsedMicroseconds(gpu_upload_start);
    }
    LOG_INFO(L"Texture artifact {}: {} (artifact_path={}, artifact_read={} us, native_import={} us, "
             L"runtime_create={} us, gpu_upload={} us, artifact_write={} us, artifact_size={} bytes)",
             loaded_from_artifact ? L"hit" : L"miss", source_system_path, artifact_path, artifact_read_us,
             native_import_us, runtime_create_us, gpu_upload_us, artifact_write_us, artifact_size);
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
            doc._import_setting = *tex_setting;
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
    MaterialAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    const Guid shader_guid(doc._shader_guid);
    Ref<Shader> shader_asset = shader_guid.IsEmpty() ? nullptr : context._resource_mgr->Load<Shader>(shader_guid);
    Shader *shader = shader_asset.get();
    if (shader == nullptr && !shader_guid.IsEmpty())
        LOG_WARNING(L"Load material with path: {} has missing shader reference {}", sys_path, ToWChar(doc._shader_guid));

    Ref<Material> mat = MakeRef<Material>(shader, doc._header._asset_name);
    mat->SetShaderGuid(shader_guid);

    mat->SavedKeyworkds().clear();
    for (auto &kw : doc._keywords)
    {
        if (!kw.empty()) mat->SavedKeyworkds().insert(kw);
    }
    if (shader != nullptr)
    {
        mat->Construct(true);
        for (auto &prop : doc._float_properties)
            mat->SetFloat(prop._name, prop._value);
        for (auto &prop : doc._vector_properties)
            mat->SetVector(prop._name, prop._value);
        for (auto &prop : doc._uint_properties)
            mat->SetInt(prop._name, prop._value);
        for (auto &prop : doc._int_vector_properties)
            mat->SetVector(prop._name, prop._value);
    }

    for (auto &prop : doc._texture_properties)
    {
        if (prop._texture_guid.empty())
            continue;
        const Guid texture_guid(prop._texture_guid);
        mat->SetTextureGuid(prop._name, texture_guid);
        auto texture_asset_path = ResourceMgr::Get().GuidToAssetPath(texture_guid);
        if (!texture_asset_path.empty())
        {
            Ref<Texture2D> texture = context._resource_mgr->Load<Texture2D>(texture_asset_path);
            if (texture != nullptr)
                mat->SetTexture(prop._name, texture.get());
            else
                LOG_WARNING("Load material: {}, property {} has missing texture reference {}", mat->Name(),
                            prop._name, prop._texture_guid);
        }
        else
        {
            LOG_WARNING("Load material: {}, property {} failed!", mat->Name(), prop._name);
        }
    }

    if (mat->IsStandardLit())
    {
        mat->SurfaceType((ESurfaceType)mat->GetUint("_surface"));
        mat->MaterialID((EMaterialID)mat->GetUint("_MaterialID"));
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
    Guid shader_guid = mat->ShaderGuid();
    if (shader_guid.IsEmpty() && mat->GetShader() != nullptr)
        shader_guid = context._resource_mgr->GetAssetGuid(mat->GetShader());
    doc._shader_guid = shader_guid.ToString();
    if (!shader_guid.IsEmpty())
        doc._header._dependencies.emplace_back(shader_guid, EAssetDependencyType::kHard);
    doc._keywords.assign(mat->SavedKeyworkds().begin(), mat->SavedKeyworkds().end());

    auto float_props = mat->GetShader() != nullptr ? mat->GetAllFloatValue() : List<std::tuple<String, float>>{};
    auto vector_props = mat->GetShader() != nullptr ? mat->GetAllVectorValue() : List<std::tuple<String, Vector4f>>{};
    auto int_vector_props = mat->GetShader() != nullptr ? mat->GetAllIntVectorValue() : List<std::tuple<String, Vector4Int>>{};
    auto uint_props = mat->GetShader() != nullptr ? mat->GetAllUintValue() : List<std::tuple<String, u32>>{};

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
            Guid tex_guid = mat->TextureGuid(prop->_value_name);
            if (tex)
            {
                Asset *linked_asset = context._resource_mgr->GetLinkedAsset(tex);
                if (linked_asset && linked_asset->_asset_type == Texture2D::StaticType())
                    tex_guid = linked_asset->GetGuid();
                else if (tex_guid.IsEmpty())
                {
                    AL_ASSERT(true);
                    LOG_ERROR("Texture2D {} hasn't a linked asset or asset type error!", tex->Name());
                }
            }
            AssetTextureBinding entry;
            entry._name = prop->_value_name;
            entry._texture_guid = tex_guid == Guid::EmptyGuid() ? String{} : tex_guid.ToString();
            doc._texture_properties.push_back(entry);
            if (!tex_guid.IsEmpty())
                doc._header._dependencies.emplace_back(tex_guid, EAssetDependencyType::kHard);
        }
    }
    if (props.empty())
    {
        for (const auto &[name, guid] : mat->TextureGuids())
        {
            if (guid.IsEmpty())
                continue;
            AssetTextureBinding entry;
            entry._name = name;
            entry._texture_guid = guid.ToString();
            doc._texture_properties.push_back(std::move(entry));
            doc._header._dependencies.emplace_back(guid, EAssetDependencyType::kHard);
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
// SkeletonAssetHandler
// ============================================================

const Type *SkeletonAssetHandler::AssetType() const
{
    return SkeletonAsset::StaticType();
}

Scope<Asset> SkeletonAssetHandler::Load(const AssetLoadContext &context)
{
    SkeletonAssetDocument document;
    if (!LoadAssetDocument(context._system_path, document))
        return nullptr;

    auto skeleton_asset = MakeRef<SkeletonAsset>();
    skeleton_asset->Name(document._header._asset_name);
    Skeleton &skeleton = skeleton_asset->GetSkeletonMutable();
    skeleton.Clear();
    for (const SkeletonJointDocument &joint_document : document._joints)
    {
        Joint joint;
        joint._name = joint_document._name;
        joint._parent = joint_document._parent;
        joint._inv_bind_pos = joint_document._inverse_bind_pose;
        skeleton.AddJoint(joint);
        skeleton.SetBindPoseLocalTransform(skeleton.JointNum() - 1u, joint_document._bind_local_transform);
    }
    skeleton_asset->Rebuild();

    auto asset = MakeScope<Asset>(Guid(document._header._guid), SkeletonAsset::StaticType(), context._asset_path);
    asset->_p_obj = std::move(skeleton_asset);
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool SkeletonAssetHandler::Save(const AssetSaveContext &context)
{
    const SkeletonAsset *skeleton_asset = context._asset->As<SkeletonAsset>();
    if (skeleton_asset == nullptr)
        return false;

    SkeletonAssetDocument document;
    document._header = MakeAssetDocumentHeader(context._asset);
    const Skeleton &skeleton = skeleton_asset->GetSkeleton();
    document._joints.reserve(skeleton.JointNum());
    for (u32 index = 0u; index < skeleton.JointNum(); ++index)
    {
        const Joint &joint = skeleton[index];
        SkeletonJointDocument &joint_document = document._joints.emplace_back();
        joint_document._name = joint._name;
        joint_document._parent = joint._parent;
        joint_document._inverse_bind_pose = joint._inv_bind_pos;
        joint_document._bind_local_transform = skeleton.GetBindPose().GetLocalTransform(index);
    }
    return SaveAssetDocument(context._system_path, document);
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
    List<Ref<AnimationClip>> clips;
    MeshAssetDocument doc;
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;

    auto file = ToWChar(doc._file);
    auto resolved_file = ResolveExternalAssetPath(context._asset_path, file);
    MeshImportSetting setting = doc._import_setting;
    setting._import_flag |= MeshImportSetting::kImportFlagMesh;
    setting._mesh_name = doc._inner_file_name;
    if (!doc._skeleton.IsEmpty())
        setting._skeleton = doc._skeleton;

    const WString source_system_path = context._resource_mgr->GetResSysPath(resolved_file);
    AssetArtifactKey artifact_key;
    SourceFingerprint source_fingerprint;
    artifact_key._importer_version = 3u;
    artifact_key._artifact_version = kMeshArtifactVersion;
    artifact_key._import_setting_hash = HashMeshImportSetting(setting);
    artifact_key._dependency_hash = CalculateMeshDependencyHash(source_system_path);

    List<Ref<Mesh>> mesh_list;
    bool loaded_from_artifact = false;
    u64 artifact_read_us = 0u;
    u64 native_import_us = 0u;
    u64 runtime_create_us = 0u;
    u64 gpu_upload_us = 0u;
    u64 artifact_write_us = 0u;
    u64 artifact_size = 0u;
    WString artifact_path;
    if (CalculateSourceFingerprint(source_system_path, source_fingerprint))
    {
        artifact_key._source_hash = source_fingerprint._content_hash;
        if (context._derived_data_cache != nullptr)
            artifact_path = context._derived_data_cache->GetArtifactPath(Guid(doc._header._guid), artifact_key);
        Vector<u8> artifact_data;
        const auto artifact_read_start = std::chrono::steady_clock::now();
        const bool artifact_loaded = context._derived_data_cache != nullptr &&
                                     context._derived_data_cache->TryLoad(Guid(doc._header._guid), artifact_key,
                                                                          artifact_data);
        artifact_read_us = ElapsedMicroseconds(artifact_read_start);
        if (artifact_loaded)
        {
            artifact_size = artifact_data.size();
            const auto runtime_create_start = std::chrono::steady_clock::now();
            MeshArtifact artifact;
            if (DeserializeMeshArtifact(artifact_data, artifact_key, artifact))
            {
                auto mesh = CreateMeshFromArtifact(artifact);
                if (mesh != nullptr)
                {
                    mesh_list.emplace_back(std::move(mesh));
                    loaded_from_artifact = true;
                    LOG_INFO(L"Mesh artifact cache hit: {}", source_system_path);
                }
            }
            runtime_create_us = ElapsedMicroseconds(runtime_create_start);
        }
    }

    if (!loaded_from_artifact)
    {
        LOG_INFO(L"Mesh artifact cache miss: {}", source_system_path);
        const auto native_import_start = std::chrono::steady_clock::now();
        mesh_list = std::move(context._resource_mgr->LoadExternalMesh(resolved_file, setting, clips));
        native_import_us = ElapsedMicroseconds(native_import_start);
        if (!mesh_list.empty() && artifact_key._source_hash != 0u && context._derived_data_cache != nullptr)
        {
            const auto artifact_write_start = std::chrono::steady_clock::now();
            MeshArtifact artifact;
            Vector<u8> serialized_artifact;
            if (BuildMeshArtifact(*mesh_list.front(), artifact) &&
                SerializeMeshArtifact(artifact, artifact_key, serialized_artifact))
            {
                artifact_size = serialized_artifact.size();
                context._derived_data_cache->Store(Guid(doc._header._guid), artifact_key, serialized_artifact);
            }
            artifact_write_us = ElapsedMicroseconds(artifact_write_start);
        }
    }
    if (mesh_list.empty())
        return nullptr;
    if (!doc._skeleton.IsEmpty())
    {
        Ref<SkeletonAsset> skeleton_asset = context._resource_mgr->GetRef<SkeletonAsset>(doc._skeleton);
        if (skeleton_asset == nullptr)
            skeleton_asset = context._resource_mgr->Load<SkeletonAsset>(doc._skeleton);
        auto *skeleton_mesh = dynamic_cast<SkeletonMesh *>(mesh_list.front().get());
        if (skeleton_mesh == nullptr)
        {
            LOG_ERROR("Mesh {} references an invalid SkeletonAsset {}", ToChar(context._asset_path),
                      doc._skeleton.ToString());
            return nullptr;
        }
        if (skeleton_asset != nullptr)
            skeleton_mesh->SetSkeletonAsset(doc._skeleton, std::move(skeleton_asset));
        else
        {
            skeleton_mesh->SetSkeletonAsset(doc._skeleton, nullptr);
            LOG_WARNING("Mesh {} has missing SkeletonAsset reference {}", ToChar(context._asset_path),
                        doc._skeleton.ToString());
        }
    }
    if (loaded_from_artifact)
    {
        const auto gpu_upload_start = std::chrono::steady_clock::now();
        mesh_list.front()->Apply();
        gpu_upload_us = ElapsedMicroseconds(gpu_upload_start);
    }
    LOG_INFO(L"Mesh artifact {}: {} (artifact_path={}, artifact_read={} us, native_import={} us, "
             L"runtime_create={} us, gpu_upload={} us, artifact_write={} us, artifact_size={} bytes)",
             loaded_from_artifact ? L"hit" : L"miss", source_system_path, artifact_path, artifact_read_us,
             native_import_us, runtime_create_us, gpu_upload_us, artifact_write_us, artifact_size);

    bool is_sk_mesh = dynamic_cast<SkeletonMesh *>(mesh_list.front().get()) != nullptr;
    auto asset = MakeScope<Asset>(Guid(doc._header._guid),Mesh::StaticType(),context._asset_path);
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

bool MeshAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    auto *target_mesh = target.As<Render::Mesh>();
    const auto *source_mesh = source.As<Render::Mesh>();
    if (target_mesh == nullptr || source_mesh == nullptr || target_mesh == source_mesh)
        return false;
    CopyMeshData(*source_mesh, *target_mesh);
    target_mesh->Apply();
    return true;
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
            doc._import_setting = *mesh_setting;
        }
    }
    if (const auto *skeleton_mesh = dynamic_cast<const SkeletonMesh *>(context._asset->_p_obj.get()))
    {
        const Guid &skeleton_guid = skeleton_mesh->GetSkeletonAsset().GetGuid();
        doc._skeleton = skeleton_guid;
        if (!skeleton_guid.IsEmpty())
            doc._header._dependencies.emplace_back(skeleton_guid, EAssetDependencyType::kHard);
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

bool SkeletonMeshAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    auto *target_mesh = target.As<Render::SkeletonMesh>();
    const auto *source_mesh = source.As<Render::SkeletonMesh>();
    if (target_mesh == nullptr || source_mesh == nullptr || target_mesh == source_mesh)
        return false;
    CopyMeshData(*source_mesh, *target_mesh);
    target_mesh->SetBoneWeights(source_mesh->GetBoneWeights());
    target_mesh->SetBoneIndices(source_mesh->GetBoneIndices());
    target_mesh->SetMeshBindGlobalTransform(source_mesh->GetMeshBindGlobalTransform());
    target_mesh->SetSkeletonAsset(source_mesh->GetSkeletonAsset().GetGuid(),
                                  source_mesh->GetSkeletonAsset().Get());
    target_mesh->Apply();
    return true;
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
            doc._import_setting = *mesh_setting;
        }
    }
    if (const auto *skeleton_mesh = dynamic_cast<const SkeletonMesh *>(context._asset->_p_obj.get()))
    {
        const Guid &skeleton_guid = skeleton_mesh->GetSkeletonAsset().GetGuid();
        doc._skeleton = skeleton_guid;
        if (!skeleton_guid.IsEmpty())
            doc._header._dependencies.emplace_back(skeleton_guid, EAssetDependencyType::kHard);
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

const Type *PrefabAssetHandler::AssetType() const
{
    return PrefabAssetDocument::StaticType();
}

Scope<Asset> PrefabAssetHandler::Load(const AssetLoadContext &context)
{
    auto prefab = MakeRef<PrefabAssetDocument>();
    if (!LoadAssetDocument(context._system_path, *prefab))
        return nullptr;

    // Object initializes _name to object_xxx. Restore the persisted asset name after loading;
    // otherwise the Asset Browser changes the displayed name as soon as the prefab is loaded.
    const String prefab_name = !prefab->_header._asset_name.empty()
        ? prefab->_header._asset_name
        : ToChar(PathUtils::GetFileName(context._asset_path).c_str());
    prefab->Name(prefab_name);

    auto asset = MakeScope<Asset>(Guid(prefab->_header._guid), PrefabAssetDocument::StaticType(), context._asset_path);
    asset->_asset_path = context._asset_path;
    asset->_asset_type = PrefabAssetDocument::StaticType();
    asset->_p_obj = std::move(prefab);
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool PrefabAssetHandler::Save(const AssetSaveContext &context)
{
    auto *prefab = context._asset->As<PrefabAssetDocument>();
    if (prefab == nullptr)
        return false;

    prefab->_header = MakeAssetDocumentHeader(context._asset);
    auto add_dependency = [&header = prefab->_header](const Guid &guid)
    {
        if (guid.IsEmpty() || std::any_of(header._dependencies.begin(), header._dependencies.end(),
                                          [&guid](const AssetDependency &dependency) { return dependency._guid == guid; }))
            return;
        header._dependencies.emplace_back(AssetDependency{guid, EAssetDependencyType::kHard});
    };
    auto add_dependency_string = [&add_dependency](const String &guid_string)
    {
        if (!guid_string.empty())
            add_dependency(Guid(guid_string));
    };
    for (const PrefabEntityDocument &entity : prefab->_entities)
    {
        const SceneEntityDocument &document = entity._entity;
        if (document._has_script_component)
            add_dependency(document._script_component._script_asset);
        if (document._has_static_mesh_component)
        {
            add_dependency_string(document._static_mesh_component._mesh_guid);
            for (const String &guid : document._static_mesh_component._material_guids)
                add_dependency_string(guid);
        }
        if (document._has_skeleton_mesh_component)
        {
            add_dependency_string(document._skeleton_mesh_component._mesh_guid);
            for (const String &guid : document._skeleton_mesh_component._material_guids)
                add_dependency_string(guid);
        }
        if (document._has_animator_component)
        {
            add_dependency_string(document._animator_component._controller_guid);
            add_dependency_string(document._animator_component._clip_guid);
        }
        if (document._has_sprite_renderer_component)
        {
            add_dependency_string(document._sprite_renderer_component._sprite_guid);
            add_dependency_string(document._sprite_renderer_component._material_guid);
        }
    }
    return SaveAssetDocument(context._system_path, *prefab);
}

Scope<Asset> SceneAssetHandler::Load(const AssetLoadContext &context)
{
    WString sys_path = context._system_path;
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
            tag->_tag = entity_doc._tag_component._tag;
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
            component._mesh_guid = entity_doc._static_mesh_component._mesh_guid.empty()
                ? Guid::EmptyGuid() : Guid(entity_doc._static_mesh_component._mesh_guid);
            component._material_guids.clear();
            for (const String &guid : entity_doc._static_mesh_component._material_guids)
                component._material_guids.emplace_back(guid.empty() ? Guid::EmptyGuid() : Guid(guid));
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
        if (entity_doc._has_rigidbody_2d_component)
        {
            auto &component = reg.AddComponent<ECS::RigidBody2DComponent>(entity);
            reg.SetComponentEnabled<ECS::RigidBody2DComponent>(entity,
                                                                !is_component_disabled(entity_doc, "RigidBody2DComponent"));
            component._type = entity_doc._rigidbody_2d_component._type;
            component._gravity_scale = entity_doc._rigidbody_2d_component._gravity_scale;
            component._linear_damping = entity_doc._rigidbody_2d_component._linear_damping;
            component._angular_damping = entity_doc._rigidbody_2d_component._angular_damping;
            component._fixed_rotation = entity_doc._rigidbody_2d_component._fixed_rotation;
            component._continuous = entity_doc._rigidbody_2d_component._continuous;
            component._allow_sleep = entity_doc._rigidbody_2d_component._allow_sleep;
        }
        if (entity_doc._has_collider_2d_component)
        {
            auto &component = reg.AddComponent<ECS::Collider2DComponent>(entity);
            reg.SetComponentEnabled<ECS::Collider2DComponent>(entity,
                                                               !is_component_disabled(entity_doc, "Collider2DComponent"));
            component._preset = entity_doc._collider_2d_component._preset;
            component._collision_profile = entity_doc._collider_2d_component._collision_profile;
            component._shapes = entity_doc._collider_2d_component._shapes;
        }
        if (entity_doc._has_skeleton_mesh_component)
        {
            auto &component = reg.AddComponent<ECS::CSkeletonMesh>(entity);
            component._mesh_guid = entity_doc._skeleton_mesh_component._mesh_guid.empty()
                ? Guid::EmptyGuid() : Guid(entity_doc._skeleton_mesh_component._mesh_guid);
            component._material_guids.clear();
            for (const String &guid : entity_doc._skeleton_mesh_component._material_guids)
                component._material_guids.emplace_back(guid.empty() ? Guid::EmptyGuid() : Guid(guid));
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
        }
        if (entity_doc._has_animator_component)
        {
            auto &component = reg.AddComponent<ECS::AnimatorComponent>(entity);
            reg.SetComponentEnabled<ECS::AnimatorComponent>(entity, !is_component_disabled(entity_doc, "AnimatorComponent"));
            if (!entity_doc._animator_component._controller_guid.empty())
                component._controller = Guid(entity_doc._animator_component._controller_guid);
            if (!entity_doc._animator_component._clip_guid.empty())
                component._clip = Guid(entity_doc._animator_component._clip_guid);
            component._speed = entity_doc._animator_component._speed;
            component._play_on_awake = entity_doc._animator_component._play_on_awake;
            component._root_motion_mode = static_cast<ERootMotionMode>(entity_doc._animator_component._root_motion_mode);
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
            component._sprite_guid = entity_doc._sprite_renderer_component._sprite_guid.empty()
                ? Guid::EmptyGuid() : Guid(entity_doc._sprite_renderer_component._sprite_guid);
            component._material_guid = entity_doc._sprite_renderer_component._material_guid.empty()
                ? Guid::EmptyGuid() : Guid(entity_doc._sprite_renderer_component._material_guid);
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

    loaded_scene->MutablePrefabInstances() = doc._prefab_instances;

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
    doc._prefab_instances = scene->PrefabInstances();

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

        const auto sibling_it = entity_sibling_index.find(entity);
        const u32 sibling_index = sibling_it != entity_sibling_index.end() ? sibling_it->second : 0u;
        SceneEntityDocument entity_doc = EntitySerializer::BuildEntityDocument(*scene, entity, sibling_index);
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
    const auto load_start = std::chrono::steady_clock::now();
    WString sys_path = context._system_path;
    AssetDocumentHeader header;
    const bool has_header = LoadAssetDocumentHeader(sys_path, header);
    AssetArtifactKey artifact_key;
    artifact_key._importer_version = 1u;
    artifact_key._artifact_version = kAnimationClipArtifactVersion;
    SourceFingerprint source_fingerprint;
    const bool has_source_fingerprint = CalculateSourceFingerprint(sys_path, source_fingerprint);
    if (has_source_fingerprint)
        artifact_key._source_hash = source_fingerprint._content_hash;

    if (has_header && has_source_fingerprint && context._derived_data_cache != nullptr)
    {
        Vector<u8> artifact_data;
        const auto artifact_read_start = std::chrono::steady_clock::now();
        const bool artifact_loaded = context._derived_data_cache->TryLoad(Guid(header._guid), artifact_key, artifact_data);
        const u64 artifact_read_us = ElapsedMicroseconds(artifact_read_start);
        if (artifact_loaded)
        {
            AnimationClipArtifact artifact;
            const auto runtime_create_start = std::chrono::steady_clock::now();
            const bool artifact_valid = DeserializeAnimationClipArtifact(artifact_data, artifact_key, artifact);
            Ref<AnimationClip> loaded_clip = artifact_valid ? CreateAnimationClipFromArtifact(artifact) : nullptr;
            const u64 runtime_create_us = ElapsedMicroseconds(runtime_create_start);
            if (loaded_clip != nullptr)
            {
                auto asset = MakeScope<Asset>(Guid(header._guid), AnimationClip::StaticType(), context._asset_path);
                asset->_p_obj = loaded_clip;
                asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
                AnimationClipLibrary::AddClip(loaded_clip);
                LOG_INFO(L"AnimationClip artifact cache hit: {} (read {} us, build {} us)", sys_path,
                         artifact_read_us, runtime_create_us);
                return asset;
            }
            LOG_WARNING(L"AnimationClip artifact rejected, rebuilding from JSON: {}", sys_path);
        }
    }

    AnimationClipAssetDocument doc;
    const auto json_load_start = std::chrono::steady_clock::now();
    if (!LoadAssetDocument(sys_path, doc))
        return nullptr;
    const u64 json_load_us = ElapsedMicroseconds(json_load_start);

    Ref<AnimationClip> loaded_clip = MakeRef<AnimationClip>();
    loaded_clip->Name(!doc._clip_name.empty() ? doc._clip_name : doc._header._asset_name);
    loaded_clip->FrameCount(doc._frame_count);
    loaded_clip->Duration(doc._duration);
    loaded_clip->FrameRate(doc._frame_rate);
    const f32 frame_duration = doc._frame_duration > 0.0f ? doc._frame_duration
        : ((doc._frame_count > 1u && doc._duration > 0.0f) ?
           (doc._duration / static_cast<f32>(doc._frame_count - 1u)) : 0.0f);
    loaded_clip->FrameDuration(frame_duration);
    loaded_clip->IsLooping(doc._is_looping);
    loaded_clip->SetRootMotionSettings(doc._root_motion);
    loaded_clip->SkeletonGuid(doc._skeleton);
    loaded_clip->PreviewMeshGuid(doc._preview_mesh_guid);
    loaded_clip->StartTime(0.0f);
    loaded_clip->EndTime(doc._duration);

    for (const auto &track_doc : doc._tracks)
    {
        auto &track = (*loaded_clip)[track_doc._joint_index];
        auto &pos_track = track.GetPositionTrack();
        pos_track.Resize(static_cast<u32>(track_doc._position_keys.size()));
        for (u32 key_index = 0u; key_index < track_doc._position_keys.size(); ++key_index)
        {
            const auto &key_doc = track_doc._position_keys[key_index];
            auto frame = TrackHelpers::FromVector(key_doc._value);
            frame._time = key_doc._time;
            pos_track[key_index] = frame;
        }

        auto &rot_track = track.GetRotationTrack();
        rot_track.Resize(static_cast<u32>(track_doc._rotation_keys.size()));
        for (u32 key_index = 0u; key_index < track_doc._rotation_keys.size(); ++key_index)
        {
            const auto &key_doc = track_doc._rotation_keys[key_index];
            auto frame = TrackHelpers::FromQuaternion(key_doc._value);
            frame._time = key_doc._time;
            rot_track[key_index] = frame;
        }

        auto &scale_track = track.GetScaleTrack();
        scale_track.Resize(static_cast<u32>(track_doc._scale_keys.size()));
        for (u32 key_index = 0u; key_index < track_doc._scale_keys.size(); ++key_index)
        {
            const auto &key_doc = track_doc._scale_keys[key_index];
            auto frame = TrackHelpers::FromVector(key_doc._value);
            frame._time = key_doc._time;
            scale_track[key_index] = frame;
        }
    }
    for (const auto &frame_doc : doc._sprite_frames)
        loaded_clip->SpriteTrack().AddFrame(SpriteKeyFrame{frame_doc._time, frame_doc._sprite});
    for (const auto &event_doc : doc._events)
        loaded_clip->AddEvent(AnimationEvent{event_doc._time, event_doc._event_id, event_doc._kind});
    if (doc._duration <= 0.0f)
    {
        loaded_clip->RecalculateDuration();
    }

    if (has_source_fingerprint && has_header && context._derived_data_cache != nullptr)
    {
        AnimationClipArtifact artifact;
        Vector<u8> serialized_artifact;
        if (BuildAnimationClipArtifact(*loaded_clip, artifact) &&
            SerializeAnimationClipArtifact(artifact, artifact_key, serialized_artifact))
        {
            if (context._derived_data_cache->Store(Guid(header._guid), artifact_key, serialized_artifact))
                LOG_INFO(L"AnimationClip artifact rebuilt: {} ({} bytes)", sys_path, serialized_artifact.size());
        }
    }

    LOG_INFO(L"AnimationClip JSON load: {} (load {} us, total {} us)", sys_path, json_load_us,
             ElapsedMicroseconds(load_start));
    
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
    doc._root_motion = clip->GetRootMotionSettings();
    doc._skeleton = clip->SkeletonGuid();
    doc._preview_mesh_guid = clip->PreviewMeshGuid();
    if (!doc._skeleton.IsEmpty())
        doc._header._dependencies.push_back(AssetDependency{doc._skeleton, EAssetDependencyType::kHard});
    if (!doc._preview_mesh_guid.IsEmpty())
        doc._header._dependencies.push_back(AssetDependency{doc._preview_mesh_guid, EAssetDependencyType::kHard});
    doc._tracks.reserve(clip->Size());
    doc._sprite_frames.reserve(clip->SpriteTrack().Frames().size());
    doc._events.reserve(clip->Events().size());

    for (u32 index = 0u; index < clip->Size(); ++index)
    {
        const TransformTrack &track = clip->GetTrackAtIndex(index);
        const auto &pos_track = track.GetPositionTrack();
        const auto &rot_track = track.GetRotationTrack();
        const auto &scale_track = track.GetScaleTrack();

        AnimationClipTrackDocument track_doc;
        track_doc._joint_index = clip->GetIdAtIndex(index);
        track_doc._position_keys.reserve(pos_track.Size());
        for (u32 key_index = 0u; key_index < pos_track.Size(); ++key_index)
        {
            AnimationVectorKeyDocument key_doc;
            key_doc._time = pos_track[key_index]._time;
            key_doc._value = TrackHelpers::ToVector(pos_track[key_index]);
            track_doc._position_keys.emplace_back(std::move(key_doc));
        }
        track_doc._rotation_keys.reserve(rot_track.Size());
        for (u32 key_index = 0u; key_index < rot_track.Size(); ++key_index)
        {
            AnimationQuaternionKeyDocument key_doc;
            key_doc._time = rot_track[key_index]._time;
            key_doc._value = TrackHelpers::ToQuaternion(rot_track[key_index]);
            track_doc._rotation_keys.emplace_back(std::move(key_doc));
        }
        track_doc._scale_keys.reserve(scale_track.Size());
        for (u32 key_index = 0u; key_index < scale_track.Size(); ++key_index)
        {
            AnimationVectorKeyDocument key_doc;
            key_doc._time = scale_track[key_index]._time;
            key_doc._value = TrackHelpers::ToVector(scale_track[key_index]);
            track_doc._scale_keys.emplace_back(std::move(key_doc));
        }
        doc._tracks.emplace_back(std::move(track_doc));
    }
    for (const auto &frame : clip->SpriteTrack().Frames())
    {
        AnimationSpriteFrameDocument frame_doc;
        frame_doc._time = frame._time;
        frame_doc._sprite = frame._sprite;
        doc._sprite_frames.emplace_back(std::move(frame_doc));
        if (!frame._sprite.IsEmpty())
            doc._header._dependencies.push_back(AssetDependency{frame._sprite, EAssetDependencyType::kHard});
    }
    for (const auto &event : clip->Events())
    {
        AnimationEventDocument event_doc;
        event_doc._time = event._time;
        event_doc._event_id = event._event_id;
        event_doc._kind = event._kind;
        doc._events.emplace_back(std::move(event_doc));
    }

    if (!SaveAssetDocument(sys_path, doc))
    {
        LOG_ERROR(L"Save animclip failed to {}", sys_path);
        return false;
    }
    LOG_INFO(L"Save animclip to {}", sys_path);
    return true;
}

bool AnimationClipAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    auto *target_clip = target.As<AnimationClip>();
    const auto *source_clip = source.As<AnimationClip>();
    if (target_clip == nullptr || source_clip == nullptr)
        return false;

    target_clip->CopyFrom(*source_clip);
    return true;
}

// ============================================================
// AnimationControllerAssetHandler
// ============================================================

const Type *AnimationControllerAssetHandler::AssetType() const
{
    return AnimationControllerAsset::StaticType();
}

Scope<Asset> AnimationControllerAssetHandler::Load(const AssetLoadContext &context)
{
    AnimationControllerAssetDocument doc;
    if (!LoadAssetDocument(context._system_path, doc))
        return nullptr;

    Ref<AnimationControllerAsset> controller = MakeRef<AnimationControllerAsset>(
        !doc._header._asset_name.empty() ? doc._header._asset_name : "AnimationControllerAsset");
    controller->Parameters() = doc._parameters;
    controller->States() = doc._states;
    controller->Transitions() = doc._transitions;
    controller->AnyStateTransitions() = doc._any_state_transitions;
    controller->EntryState(doc._entry_state);

    auto asset = MakeScope<Asset>(Guid(doc._header._guid), AnimationControllerAsset::StaticType(), context._asset_path);
    asset->_p_obj = controller;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool AnimationControllerAssetHandler::Save(const AssetSaveContext &context)
{
    const auto *controller = context._asset->As<AnimationControllerAsset>();
    if (controller == nullptr)
        return false;

    AnimationControllerAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._parameters = controller->Parameters();
    doc._states = controller->States();
    doc._transitions = controller->Transitions();
    doc._any_state_transitions = controller->AnyStateTransitions();
    doc._entry_state = controller->EntryState();
    for (const auto &state : doc._states)
    {
        const Guid &guid = state._motion._asset;
        if (guid.IsEmpty() || std::any_of(doc._header._dependencies.begin(), doc._header._dependencies.end(),
                                          [&guid](const AssetDependency &dependency) { return dependency._guid == guid; }))
            continue;
        doc._header._dependencies.emplace_back(AssetDependency{guid, EAssetDependencyType::kHard});
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save animation controller failed to {}", context._system_path);
        return false;
    }
    LOG_INFO(L"Save animation controller to {}", context._system_path);
    return true;
}

bool AnimationControllerAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    auto *target_controller = target.As<AnimationControllerAsset>();
    const auto *source_controller = source.As<AnimationControllerAsset>();
    if (target_controller == nullptr || source_controller == nullptr)
        return false;

    target_controller->Parameters() = source_controller->Parameters();
    target_controller->States() = source_controller->States();
    target_controller->Transitions() = source_controller->Transitions();
    target_controller->AnyStateTransitions() = source_controller->AnyStateTransitions();
    target_controller->EntryState(source_controller->EntryState());
    return true;
}

// ============================================================
// BlendSpaceAssetHandler
// ============================================================

const Type *BlendSpaceAssetHandler::AssetType() const
{
    return BlendSpaceAsset::StaticType();
}

Scope<Asset> BlendSpaceAssetHandler::Load(const AssetLoadContext &context)
{
    BlendSpaceAssetDocument doc;
    if (!LoadAssetDocument(context._system_path, doc))
        return nullptr;

    Ref<BlendSpaceAsset> blend_space = MakeRef<BlendSpaceAsset>(
        !doc._header._asset_name.empty() ? doc._header._asset_name : "BlendSpaceAsset");
    blend_space->Samples() = doc._samples;
    blend_space->XRange(doc._x_range);
    blend_space->YRange(doc._y_range);
    blend_space->Is2D(doc._is_2d);
    std::sort(blend_space->Samples().begin(), blend_space->Samples().end(),
              [](const BlendSpaceSample &lhs, const BlendSpaceSample &rhs)
              { return lhs._position.x < rhs._position.x; });

    auto asset = MakeScope<Asset>(Guid(doc._header._guid), BlendSpaceAsset::StaticType(), context._asset_path);
    asset->_p_obj = blend_space;
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool BlendSpaceAssetHandler::Save(const AssetSaveContext &context)
{
    const auto *blend_space = context._asset->As<BlendSpaceAsset>();
    if (blend_space == nullptr)
        return false;

    BlendSpaceAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._samples = blend_space->Samples();
    doc._x_range = blend_space->XRange();
    doc._y_range = blend_space->YRange();
    doc._is_2d = blend_space->Is2D();
    for (const auto &sample : doc._samples)
    {
        if (sample._clip.IsEmpty() || std::any_of(doc._header._dependencies.begin(), doc._header._dependencies.end(),
                                                   [&sample](const AssetDependency &dependency)
                                                   { return dependency._guid == sample._clip; }))
            continue;
        doc._header._dependencies.emplace_back(AssetDependency{sample._clip, EAssetDependencyType::kHard});
    }

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save blend space failed, {}", context._system_path);
        return false;
    }
    LOG_INFO(L"Save blend space to {}", context._system_path);
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

// ============================================================
// WidgetAssetHandler
// ============================================================

const Type *WidgetAssetHandler::AssetType() const
{
    return WidgetAsset::StaticType();
}

Scope<Asset> WidgetAssetHandler::Load(const AssetLoadContext &context)
{
    WidgetAssetDocument doc;
    if (!LoadAssetDocument(context._system_path, doc))
        return nullptr;

    auto widget_asset = MakeRef<WidgetAsset>(doc._header._asset_name);
    widget_asset->SetDesignSize(doc._design_size);
    widget_asset->SetRoot(std::move(doc._root));

    auto asset = MakeScope<Asset>(Guid(doc._header._guid), WidgetAsset::StaticType(), context._asset_path);
    asset->_p_obj = std::move(widget_asset);
    asset->_domain = context._resource_mgr->GetAssetPathDomain(asset->_asset_path);
    return asset;
}

bool WidgetAssetHandler::Save(const AssetSaveContext &context)
{
    const WidgetAsset *widget_asset = context._asset->As<WidgetAsset>();
    if (widget_asset == nullptr)
        return false;

    WidgetAssetDocument doc;
    doc._header = MakeAssetDocumentHeader(context._asset);
    doc._design_size = widget_asset->DesignSize();
    doc._root = widget_asset->RootRef();

    if (!SaveAssetDocument(context._system_path, doc))
    {
        LOG_ERROR(L"Save widget asset failed to {}", context._system_path);
        return false;
    }
    LOG_INFO(L"Save widget asset to {}", context._system_path);
    return true;
}

bool WidgetAssetHandler::ReloadInPlace(Asset &target, const Asset &source)
{
    auto *target_widget = target.As<WidgetAsset>();
    const auto *source_widget = source.As<WidgetAsset>();
    if (target_widget == nullptr || source_widget == nullptr)
        return false;

    Ref<UI::UIElement> root = CloneUIElementTree(source_widget->RootRef());
    if (source_widget->RootRef() != nullptr && root == nullptr)
        return false;
    target_widget->SetDesignSize(source_widget->DesignSize());
    target_widget->SetRoot(std::move(root));
    return true;
}

} // namespace Ailu
