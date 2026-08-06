#pragma once
#ifndef __AUTOMATION_REGISTRY_H__
#define __AUTOMATION_REGISTRY_H__

#include "Automation/AutomationTypes.h"
#include "Framework/Common/NonCopyable.h"

#include <functional>

namespace Ailu
{
    namespace Editor
    {
        class EditorAutomationRegistry;

        // Context handed to every automation handler. Fields for scene / asset /
        // adapter services are added in later phases as they come online.
        struct EditorAutomationContext
        {
            const AutomationRequestContext *_request = nullptr;
            EditorAutomationRegistry *_registry = nullptr;
        };

        enum class EAutomationPermission
        {
            kReadOnly,
            kSafeWrite,
            kDestructiveWrite,
            kExternalEffect,
        };

        struct AutomationMethodDesc
        {
            String _name;
            String _description;
            EAutomationPermission _permission = EAutomationPermission::kReadOnly;
            AutomationSchema _input_schema;
            AutomationSchema _output_schema;
        };

        using AutomationHandler = std::function<AutomationResult(
            EditorAutomationContext &,
            const AutomationObject &)>;

        using MethodVisitor = std::function<void(const AutomationMethodDesc &desc)>;

        // Holds method -> handler registrations. A single method description is
        // shared between invocation and MCP tool-schema generation.
        class EditorAutomationRegistry final : public NonCopyable
        {
        public:
            bool Register(String method, AutomationMethodDesc desc, AutomationHandler handler);

            const AutomationMethodDesc *Find(StringView method) const;
            bool Contains(StringView method) const;
            AutomationResult Invoke(StringView method, EditorAutomationContext &context, const AutomationObject &arguments) const;
            void ForEachMethod(const MethodVisitor &visitor) const;

        private:
            struct MethodEntry
            {
                AutomationMethodDesc _desc;
                AutomationHandler _handler;
            };

            HashMap<String, MethodEntry> _methods;
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_REGISTRY_H__
