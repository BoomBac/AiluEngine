#include "Inspector/ComponentEditors/TransformComponentEditor.h"
#include "Inspector/ComponentEditorRegistry.h"
#include "Inspector/ComponentEditorHelpers.h"
#include "Scene/Scene.h"

using namespace Ailu;
using namespace Ailu::UI;
using SceneManagement::SceneMgr;

namespace Ailu
{
    namespace Editor
    {
        void TransformComponentEditor::Build(ComponentEditorContext &context)
        {
            auto entity = context._entity;
            auto &r = SceneMgr::Get().ActiveScene()->GetRegister();
            auto *comp = r.GetComponent<ECS::TransformComponent>(entity);
            if (comp == nullptr || context._content == nullptr)
                return;

            auto pos_block = Editor::AddVec3InputRow(context._content, "Position", "0000");
            auto rot_block = Editor::AddVec3InputRow(context._content, "Rotation");
            auto scale_block = Editor::AddVec3InputRow(context._content, "Scale");
            for (u32 i = 0; i < 3; ++i)
            {
                _position_blocks[i] = pos_block[i];
                _rotation_blocks[i] = rot_block[i];
                _scale_blocks[i] = scale_block[i];
            }

            for (auto i = 0; i < 3; i++)
            {
                pos_block[i]->_on_content_changed += [entity, i](String content)
                {
                    auto &reg = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto tcomp = reg.GetComponent<ECS::TransformComponent>(entity); tcomp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            tcomp->_local_transform._position[i] = opt.value();
                    }
                };
                rot_block[i]->_on_content_changed += [entity, i](String content)
                {
                    auto &reg = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto tcomp = reg.GetComponent<ECS::TransformComponent>(entity); tcomp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                        {
                            Vector3f euler = Quaternion::EulerAngles(tcomp->_local_transform._rotation);
                            euler[i] = opt.value();
                            tcomp->_local_transform._rotation = Quaternion::EulerAngles(euler);
                        }
                    }
                };
                scale_block[i]->_on_content_changed += [entity, i](String content)
                {
                    auto &reg = SceneMgr::Get().ActiveScene()->GetRegister();
                    if (auto tcomp = reg.GetComponent<ECS::TransformComponent>(entity); tcomp != nullptr)
                    {
                        if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                            tcomp->_local_transform._scale[i] = opt.value();
                    }
                };
            }

            Refresh(context);
        }

        void TransformComponentEditor::Refresh(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::TransformComponent>();
            if (comp == nullptr)
                return;

            const auto set_block = [](InputBlock *block, f32 value)
            {
                if (block != nullptr && !block->IsEditing())
                    block->SetContent(std::format("{:.2f}", value), false);
            };
            set_block(_position_blocks[0], comp->_local_transform._position.x);
            set_block(_position_blocks[1], comp->_local_transform._position.y);
            set_block(_position_blocks[2], comp->_local_transform._position.z);
            const Vector3f euler = Quaternion::EulerAngles(comp->_local_transform._rotation);
            set_block(_rotation_blocks[0], euler.x);
            set_block(_rotation_blocks[1], euler.y);
            set_block(_rotation_blocks[2], euler.z);
            set_block(_scale_blocks[0], comp->_local_transform._scale.x);
            set_block(_scale_blocks[1], comp->_local_transform._scale.y);
            set_block(_scale_blocks[2], comp->_local_transform._scale.z);
        }
    }// namespace Editor
}// namespace Ailu
