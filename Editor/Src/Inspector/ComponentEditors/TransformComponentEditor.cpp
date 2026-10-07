#include "Inspector/ComponentEditors/TransformComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Scene.h"
#include "Framework/Common/Input.h"
#include "UI/UIFramework.h"
#include <cmath>

using namespace Ailu;
using namespace Ailu::UI;

namespace Ailu::Editor
{
    TransformComponentEditor::~TransformComponentEditor()
    {
        if (_command_manager != nullptr && _command_manager == g_pCommandMgr)
            _command_manager->BeforeHistoryChange().Unsubscribe(_history_subscription);
        FinishEdit();
    }

    void TransformComponentEditor::FinishEdit()
    {
        if (_pending_command != nullptr && _pending_command->HasChanges() && g_pCommandMgr != nullptr)
            g_pCommandMgr->ExecuteCommand(std::move(_pending_command));
        _pending_command.reset();
        _editing_block = nullptr;
    }

    void TransformComponentEditor::Build(ComponentEditorContext &context)
    {
        if (context._scene == nullptr || context._content == nullptr)
            return;
        if (g_pCommandMgr != nullptr && _command_manager == nullptr)
        {
            _command_manager = g_pCommandMgr;
            _history_subscription = _command_manager->BeforeHistoryChange().Subscribe([this]()
            {
                auto *block = _editing_block;
                FinishEdit();
                if (block != nullptr && UIManager::Get() != nullptr)
                    UIManager::Get()->ClearFocus(block);
            });
        }
        auto scene = std::dynamic_pointer_cast<SceneManagement::Scene>(context._scene->SharedFromThis());
        Vector<ECS::Entity> entities = context._entities;
        if (entities.empty())
            entities.push_back(context._entity);
        Vector<Guid> guids;
        for (auto entity : entities)
            if (const auto *guid = context._scene->FindEntityGuid(entity))
                guids.push_back(*guid);
        if (scene == nullptr)
            context._content->AddChild<Text>("Runtime edits are not added to Undo");
        auto position = AddVec3InputRow(context._content, "Position");
        auto rotation = AddVec3InputRow(context._content, "Rotation");
        auto scale = AddVec3InputRow(context._content, "Scale");
        for (u32 axis = 0; axis < 3; ++axis)
        {
            _position_blocks[axis] = position[axis];
            _rotation_blocks[axis] = rotation[axis];
            _scale_blocks[axis] = scale[axis];
        }
        const auto bind = [this, weak_scene = Weak<SceneManagement::Scene>(scene), entities, guids,
                           runtime_scene = scene == nullptr ? context._scene : nullptr]
            (InputBlock *block, ETransformProperty property, u32 axis)
        {
            block->_on_content_changed +=
                [this, weak_scene, entities, guids, runtime_scene, block, property, axis](String content)
            {
                auto scene = weak_scene.lock();
                const auto value = StringUtils::ParseFloat(content);
                if (!value.has_value() || !std::isfinite(*value))
                    return;
                auto *active_scene = SceneManagement::SceneMgr::Get().ActiveScene();
                // Play-mode copies are SceneMgr-owned raw objects; never retain them in the undo stack.
                if (runtime_scene != nullptr && active_scene == runtime_scene)
                {
                    bool changed = false;
                    for (const auto &guid : guids)
                    {
                        const auto entity = active_scene->FindEntity(guid);
                        if (!active_scene->IsValidEntity(entity))
                            continue;
                        auto *component = active_scene->GetRegister().GetComponent<ECS::TransformComponent>(entity);
                        if (component != nullptr)
                            changed |= TransformPropertyCommand::WriteValue(*component, property, axis, *value);
                    }
                    if (changed)
                        active_scene->MarkEdited();
                    return;
                }
                if (scene == nullptr || scene.get() != active_scene)
                    return;
                if (_editing_block != block)
                    FinishEdit();
                if (_pending_command == nullptr)
                {
                    _pending_command = MakeScope<TransformPropertyCommand>(scene, entities, property, axis);
                    _editing_block = block;
                }
                _pending_command->Preview(*value);
            };
            block->_on_focus_lost += [this]() { FinishEdit(); };
            block->OnMouseUp() += [this, block](UIEvent &)
            {
                if (!block->IsEditing())
                    FinishEdit();
            };
            block->OnKeyDown() += [this](UIEvent &event)
            {
                if (event._key_code == EKey::kRETURN)
                    FinishEdit();
            };
        };
        for (u32 axis = 0; axis < 3; ++axis)
        {
            bind(position[axis], ETransformProperty::kPosition, axis);
            bind(rotation[axis], ETransformProperty::kRotation, axis);
            bind(scale[axis], ETransformProperty::kScale, axis);
        }
        Refresh(context);
    }

    void TransformComponentEditor::Refresh(ComponentEditorContext &context)
    {
        if (context._scene == nullptr)
            return;
        if (_editing_block != nullptr && !_editing_block->IsEditing() && !_editing_block->IsDragAdjusting())
            FinishEdit();
        Vector<ECS::Entity> entities = context._entities;
        if (entities.empty())
            entities.push_back(context._entity);
        const auto refresh = [&context, &entities](InputBlock **blocks, ETransformProperty property)
        {
            Vector3f first;
            bool has_value = false;
            bool mixed[3] = {};
            for (auto entity : entities)
            {
                if (!context._scene->IsValidEntity(entity))
                    continue;
                const auto *component = context.Registry().GetComponent<ECS::TransformComponent>(entity);
                if (component == nullptr)
                    continue;
                const auto value = TransformPropertyCommand::ReadValue(*component, property);
                if (!has_value)
                {
                    first = value;
                    has_value = true;
                }
                else
                {
                    for (u32 axis = 0; axis < 3; ++axis)
                        mixed[axis] |= std::abs(first[axis] - value[axis]) > 0.00001f;
                }
            }
            for (u32 axis = 0; axis < 3; ++axis)
            {
                auto *block = blocks[axis];
                if (block != nullptr && !block->IsEditing() && !block->IsDragAdjusting())
                    block->SetContent(!has_value || mixed[axis] ? "--" : std::format("{:.2f}", first[axis]), false);
            }
        };
        refresh(_position_blocks, ETransformProperty::kPosition);
        refresh(_rotation_blocks, ETransformProperty::kRotation);
        refresh(_scale_blocks, ETransformProperty::kScale);
    }
}
