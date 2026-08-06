#pragma once
#ifndef __AUTOMATION_SCENE_SERVICE_H__
#define __AUTOMATION_SCENE_SERVICE_H__

#include "Automation/AutomationRegistry.h"

namespace Ailu
{
    namespace Editor
    {
        // Registers the read-only scene/editor tools:
        //   editor.get_state
        //   scene.query_entities
        //   scene.inspect_entity
        class AutomationSceneService final : public NonCopyable
        {
        public:
            static void Register(EditorAutomationRegistry &registry);

        private:
            static AutomationResult HandleGetState(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleQueryEntities(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleInspectEntity(EditorAutomationContext &context, const AutomationObject &arguments);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_SCENE_SERVICE_H__
