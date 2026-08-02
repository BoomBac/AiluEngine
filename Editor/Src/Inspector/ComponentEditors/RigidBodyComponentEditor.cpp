#include "Inspector/ComponentEditors/RigidBodyComponentEditor.h"
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
        void RigidBodyComponentEditor::Build(ComponentEditorContext &context)
        {
            auto *comp = context.GetComponent<ECS::CRigidBody>();
            if (comp == nullptr || context._content == nullptr)
                return;

            Editor::AddFloatInputRow(context._content, "Mass", std::format("{:.2f}", comp->_mass), [comp](f32 v)
            {
                comp->_mass = v;
                SceneMgr::Get().MarkCurSceneDirty();
            });

            {
                auto blocks = Editor::AddVec3InputRow(context._content, "Velocity",
                    std::format("{:.2f}", comp->_velocity.x),
                    std::format("{:.2f}", comp->_velocity.y),
                    std::format("{:.2f}", comp->_velocity.z),
                    [comp](int axis, f32 v)
                    {
                        comp->_velocity[axis] = v;
                        SceneMgr::Get().MarkCurSceneDirty();
                    });
            }

            {
                auto blocks = Editor::AddVec3InputRow(context._content, "Force",
                    std::format("{:.2f}", comp->_force.x),
                    std::format("{:.2f}", comp->_force.y),
                    std::format("{:.2f}", comp->_force.z),
                    [comp](int axis, f32 v)
                    {
                        comp->_force[axis] = v;
                        SceneMgr::Get().MarkCurSceneDirty();
                    });
            }

            {
                auto blocks = Editor::AddVec3InputRow(context._content, "Angular Velocity",
                    std::format("{:.2f}", comp->_angular_velocity.x),
                    std::format("{:.2f}", comp->_angular_velocity.y),
                    std::format("{:.2f}", comp->_angular_velocity.z),
                    [comp](int axis, f32 v)
                    {
                        comp->_angular_velocity[axis] = v;
                        SceneMgr::Get().MarkCurSceneDirty();
                    });
            }

            {
                auto blocks = Editor::AddVec3InputRow(context._content, "Torque",
                    std::format("{:.2f}", comp->_torque.x),
                    std::format("{:.2f}", comp->_torque.y),
                    std::format("{:.2f}", comp->_torque.z),
                    [comp](int axis, f32 v)
                    {
                        comp->_torque[axis] = v;
                        SceneMgr::Get().MarkCurSceneDirty();
                    });
            }

            Editor::AddFloatInputRow(context._content, "Inertia", std::format("{:.2f}", comp->_inertia), [comp](f32 v)
            {
                comp->_inertia = v;
                SceneMgr::Get().MarkCurSceneDirty();
            });
        }
    }// namespace Editor
}// namespace Ailu
