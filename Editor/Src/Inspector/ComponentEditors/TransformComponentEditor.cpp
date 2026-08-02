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

            auto set_block = [](InputBlock *block, f32 value)
            {
                if (!block->IsEditing())
                    block->SetContent(std::format("{:.2f}", value), false);
            };

            comp = r.GetComponent<ECS::TransformComponent>(entity);
            if (comp == nullptr)
                return;
            set_block(pos_block[0], comp->_local_transform._position.x);
            set_block(pos_block[1], comp->_local_transform._position.y);
            set_block(pos_block[2], comp->_local_transform._position.z);
            Vector3f euler = Quaternion::EulerAngles(comp->_local_transform._rotation);
            set_block(rot_block[0], euler.x);
            set_block(rot_block[1], euler.y);
            set_block(rot_block[2], euler.z);
            set_block(scale_block[0], comp->_local_transform._scale.x);
            set_block(scale_block[1], comp->_local_transform._scale.y);
            set_block(scale_block[2], comp->_local_transform._scale.z);
        }
    }// namespace Editor
}// namespace Ailu
