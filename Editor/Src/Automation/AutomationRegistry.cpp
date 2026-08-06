#include "Automation/AutomationRegistry.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        bool EditorAutomationRegistry::Register(String method, AutomationMethodDesc desc, AutomationHandler handler)
        {
            if (method.empty() || handler == nullptr)
                return false;
            auto [it, inserted] = _methods.try_emplace(std::move(method), MethodEntry{std::move(desc), std::move(handler)});
            return inserted;
        }

        const AutomationMethodDesc *EditorAutomationRegistry::Find(StringView method) const
        {
            const auto it = _methods.find(String(method));
            return it != _methods.end() ? &it->second._desc : nullptr;
        }

        bool EditorAutomationRegistry::Contains(StringView method) const
        {
            return _methods.contains(String(method));
        }

        AutomationResult EditorAutomationRegistry::Invoke(StringView method, EditorAutomationContext &context, const AutomationObject &arguments) const
        {
            const auto it = _methods.find(String(method));
            if (it == _methods.end())
            {
                return AutomationResult::Fail(AutomationErrors::kActionNotFound,
                                              std::format("automation method '{}' is not registered", method));
            }
            return it->second._handler(context, arguments);
        }

        void EditorAutomationRegistry::ForEachMethod(const MethodVisitor &visitor) const
        {
            if (visitor == nullptr)
                return;
            for (const auto &[name, entry] : _methods)
            {
                (void) name;
                visitor(entry._desc);
            }
        }
    }// namespace Editor
}// namespace Ailu
