#include "Inspector/ComponentEditors/ScriptComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Framework/Script/ScriptSystem.h"
#include "Assets/Asset.h"
#include "Assets/ScriptAsset.h"
#include "Audio/AudioClip.h"
#include "Animation/Clip.h"
#include "Render/2D/Sprite.h"
#include "Render/Mesh.h"
#include "Render/Material.h"
#include "Render/Texture.h"
#include "Framework/Common/ResourceMgr.h"
#include "UI/Container.h"
#include "UI/DragDrop.h"
#include "Scene/Scene.h"

using namespace Ailu;
using namespace Ailu::UI;
using SceneManagement::SceneMgr;

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            void MarkPropertyDirty()
            {
                SceneMgr::Get().MarkCurSceneDirty();
            }

            void AddVectorPropertyRows(UIElement *parent, ECS::ScriptPropertyData &property, int dimension)
            {
                static constexpr Array<const char *, 4> kAxisNames = {"X", "Y", "Z", "W"};
                for (int axis = 0; axis < dimension; ++axis)
                {
                    AddFloatInputRow(parent, std::format("{} {}", property._name, kAxisNames[axis]),
                                     std::format("{:.3f}", property._vector_value[axis]), [&property, axis](f32 value)
                    {
                        property._vector_value[axis] = value;
                        MarkPropertyDirty();
                    });
                }
            }

            struct AssetChoice
            {
                Guid _guid = Guid::EmptyGuid();
                String _name;
            };

            template<typename T>
            void CollectAssetChoices(Vector<AssetChoice> &choices)
            {
                for (auto it = ResourceMgr::Get().ResourceBegin<T>(); it != ResourceMgr::Get().ResourceEnd<T>(); ++it)
                {
                    T *resource = ResourceMgr::Get().IterToRefPtr<T>(it).get();
                    if (resource == nullptr)
                        continue;
                    choices.push_back({ResourceMgr::Get().GetAssetGuid(resource), resource->Name()});
                }
            }

            bool CollectTypedAssetChoices(const String &type_name, Vector<AssetChoice> &choices)
            {
                if (type_name == "Sprite") CollectAssetChoices<Render::Sprite>(choices);
                else if (type_name == "Texture2D") CollectAssetChoices<Render::Texture2D>(choices);
                else if (type_name == "Material") CollectAssetChoices<Render::Material>(choices);
                else if (type_name == "Mesh") CollectAssetChoices<Render::Mesh>(choices);
                else if (type_name == "SkeletonMesh") CollectAssetChoices<Render::SkeletonMesh>(choices);
                else if (type_name == "AnimationClip") CollectAssetChoices<AnimationClip>(choices);
                else if (type_name == "AudioClip") CollectAssetChoices<AudioClip>(choices);
                else if (type_name == "Script")
                {
                    for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
                    {
                        Asset *asset = it->second.get();
                        if (asset != nullptr && asset->_asset_type == ScriptAsset::StaticType())
                            choices.push_back({asset->GetGuid(), asset->Name()});
                    }
                }
                else return false;
                return true;
            }

            void AddAssetPropertyRow(UIElement *parent, ECS::ScriptPropertyData &property)
            {
                Vector<AssetChoice> choices;
                if (!CollectTypedAssetChoices(property._asset_type, choices))
                {
                    const String guid = property._guid_value == Guid::EmptyGuid() ? String{} : property._guid_value.ToString();
                    AddTextInputRow(parent, property._name, guid, [&property](const String &value)
                    {
                        property._guid_value = Guid(value);
                        MarkPropertyDirty();
                    });
                    return;
                }

                Vector<String> labels{"None"};
                for (const auto &choice : choices)
                    labels.push_back(choice._name);
                auto dropdown = AddDropdownRow(parent, property._name, labels);
                i32 selected_index = 0;
                for (u32 index = 0u; index < choices.size(); ++index)
                {
                    if (choices[index]._guid == property._guid_value)
                    {
                        selected_index = static_cast<i32>(index + 1u);
                        break;
                    }
                }
                dropdown->SetSelectedIndex(selected_index);
                dropdown->_on_selected_changed += [&property, choices](i32 index)
                {
                    property._guid_value = index > 0 && index - 1 < static_cast<i32>(choices.size())
                        ? choices[index - 1]._guid : Guid::EmptyGuid();
                    MarkPropertyDirty();
                };
            }
        }// namespace

        void ScriptComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::ScriptComponent>();
            if (comp == nullptr || context._content == nullptr)
                return;

            auto entity = context._entity;

            auto assign_script_asset = [entity, request_rebuild = context._request_rebuild](const Guid &script_asset)
            {
                auto *scene = SceneMgr::Get().ActiveScene();
                if (scene == nullptr)
                    return;
                auto &r = scene->GetRegister();
                auto *script_comp = r.GetComponent<ECS::ScriptComponent>(entity);
                if (script_comp == nullptr || script_comp->_script_asset == script_asset)
                    return;
                ScriptSystem::Get().DestroyComponent(scene, entity, *script_comp);
                script_comp->_script_asset = script_asset;
                script_comp->_properties.clear();
                SceneMgr::Get().MarkCurSceneDirty();
                if (request_rebuild)
                    request_rebuild();
            };

            auto script_assets = std::make_shared<Vector<Asset *>>();
            Vector<String> script_asset_names{"None"};
            for (auto it = ResourceMgr::Get().Begin(); it != ResourceMgr::Get().End(); ++it)
            {
                Asset *asset = it->second.get();
                if (asset == nullptr || asset->_asset_type != ScriptAsset::StaticType())
                    continue;
                script_assets->push_back(asset);
                script_asset_names.push_back(asset->Name());
            }

            auto script_row = context._content->AddChild<HorizontalBox>();
            script_row->AddChild<Text>("Script Asset")
                    ->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).Margin({2.0f, 0.0f, 2.0f, 2.0f});
            auto script_dropdown = script_row->AddChild<Dropdown>(script_asset_names);
            script_dropdown->GetSlotAs<LinearSlot>().SizePolicy(ESizePolicy::kFill, ESizePolicy::kAuto).Margin({10.0f, 0.0f, 2.0f, 2.0f});
            i32 selected_index = 0;
            for (u32 index = 0u; index < script_assets->size(); ++index)
            {
                if ((*script_assets)[index]->GetGuid() == comp->_script_asset)
                {
                    selected_index = static_cast<i32>(index + 1u);
                    break;
                }
            }
            script_dropdown->SetSelectedIndex(selected_index);
            script_dropdown->_on_selected_changed += [script_assets, assign_script_asset](i32 index)
            {
                assign_script_asset(index > 0 && index - 1 < static_cast<i32>(script_assets->size())
                                         ? (*script_assets)[index - 1]->GetGuid()
                                         : Guid::EmptyGuid());
            };
            DropHandler drop_handler;
            drop_handler._can_drop = [](const DragPayload &payload)
            {
                return payload._type == EDragType::kScript && payload._data != nullptr;
            };
            drop_handler._on_drop = [script_assets, assign_script_asset](const DragPayload &payload, f32, f32)
            {
                auto *asset = static_cast<Asset *>(payload._data);
                if (asset == nullptr || asset->_asset_type != ScriptAsset::StaticType())
                    return;
                assign_script_asset(asset->GetGuid());
            };
            script_dropdown->SetDropHandler(std::move(drop_handler));

            //auto hint = context._content->AddChild<Text>("Select a ScriptAsset or drop one here.");
            //hint->GetSlotAs<LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f});

            ScriptSystem::Get().SynchronizeComponentProperties(*comp);
            for (ECS::ScriptPropertyData &property : comp->_properties)
            {
                if (property._is_orphan)
                    continue;

                switch (property._type)
                {
                case ECS::EScriptPropertyType::kBool:
                    AddCheckBoxRow(context._content, property._name, property._bool_value)->_on_click += [&property](bool value)
                    {
                        property._bool_value = value;
                        MarkPropertyDirty();
                    };
                    break;
                case ECS::EScriptPropertyType::kInt:
                    AddTextInputRow(context._content, property._name, std::to_string(property._int_value), [&property](const String &content)
                    {
                        if (const auto value = StringUtils::ParseInt32(content); value.has_value())
                        {
                            property._int_value = value.value();
                            MarkPropertyDirty();
                        }
                    });
                    break;
                case ECS::EScriptPropertyType::kFloat:
                    AddFloatInputRow(context._content, property._name, std::format("{:.3f}", property._float_value), [&property](f32 value)
                    {
                        property._float_value = value;
                        MarkPropertyDirty();
                    });
                    break;
                case ECS::EScriptPropertyType::kString:
                    AddTextInputRow(context._content, property._name, property._string_value, [&property](const String &value)
                    {
                        property._string_value = value;
                        MarkPropertyDirty();
                    });
                    break;
                case ECS::EScriptPropertyType::kVector2: AddVectorPropertyRows(context._content, property, 2); break;
                case ECS::EScriptPropertyType::kVector3: AddVectorPropertyRows(context._content, property, 3); break;
                case ECS::EScriptPropertyType::kVector4:
                case ECS::EScriptPropertyType::kColor: AddVectorPropertyRows(context._content, property, 4); break;
                case ECS::EScriptPropertyType::kEntity:
                case ECS::EScriptPropertyType::kAsset:
                {
                    if (property._type == ECS::EScriptPropertyType::kAsset)
                        AddAssetPropertyRow(context._content, property);
                    else
                    {
                        const String guid = property._guid_value == Guid::EmptyGuid() ? String{} : property._guid_value.ToString();
                        AddTextInputRow(context._content, property._name, guid, [&property](const String &value)
                        {
                            property._guid_value = Guid(value);
                            MarkPropertyDirty();
                        });
                    }
                    break;
                }
                }
            }
        }

        void ScriptComponentEditor::Refresh(ComponentEditorContext &context)
        {
        }
    }// namespace Editor
}// namespace Ailu
