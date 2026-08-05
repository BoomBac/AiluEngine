#include "Common/Selection.h"
#include "Scene/Scene.h"

namespace Ailu
{
    namespace Editor
    {
        Guid Selection::FirstEntityGuid()
        {
            const ECS::Entity entity = FirstEntity();
            if (entity == ECS::kInvalidEntity)
                return Guid::EmptyGuid();
            auto *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return Guid::EmptyGuid();
            return scene->GetEntityGuid(entity);
        }
    }// namespace Editor
}// namespace Ailu
