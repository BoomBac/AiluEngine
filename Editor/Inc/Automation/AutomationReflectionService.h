#pragma once
#ifndef __AUTOMATION_REFLECTION_SERVICE_H__
#define __AUTOMATION_REFLECTION_SERVICE_H__

#include "Automation/AutomationRegistry.h"

namespace Ailu
{
    namespace Editor
    {
        // Registers the read-only reflection tool:
        //   reflection.get_type_schema
        class AutomationReflectionService final : public NonCopyable
        {
        public:
            static void Register(EditorAutomationRegistry &registry);

        private:
            static AutomationResult HandleGetTypeSchema(EditorAutomationContext &context, const AutomationObject &arguments);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_REFLECTION_SERVICE_H__
