#pragma once
#ifndef __AUTOMATION_DESTRUCTIVE_SERVICE_H__
#define __AUTOMATION_DESTRUCTIVE_SERVICE_H__

#include "Automation/AutomationRegistry.h"

namespace Ailu
{
    namespace Editor
    {
        // Registers the destructive tools (require allow_destructive). They run
        // immediately and are not undoable.
        //   scene.delete_entity
        //   scene.save
        //   asset.delete
        //   asset.reimport
        class AutomationDestructiveService final : public NonCopyable
        {
        public:
            static void Register(EditorAutomationRegistry &registry);

        private:
            static AutomationResult HandleDeleteEntity(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleSaveScene(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleDeleteAsset(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleReimportAsset(EditorAutomationContext &context, const AutomationObject &arguments);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_DESTRUCTIVE_SERVICE_H__
