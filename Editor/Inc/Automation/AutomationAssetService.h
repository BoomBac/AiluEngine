#pragma once
#ifndef __AUTOMATION_ASSET_SERVICE_H__
#define __AUTOMATION_ASSET_SERVICE_H__

#include "Automation/AutomationRegistry.h"

namespace Ailu
{
    namespace Editor
    {
        // Registers the read-only asset tools:
        //   asset.query
        //   asset.inspect
        class AutomationAssetService final : public NonCopyable
        {
        public:
            static void Register(EditorAutomationRegistry &registry);

        private:
            static AutomationResult HandleQuery(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleInspect(EditorAutomationContext &context, const AutomationObject &arguments);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_ASSET_SERVICE_H__
