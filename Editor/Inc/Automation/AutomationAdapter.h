#pragma once
#ifndef __AUTOMATION_ADAPTER_H__
#define __AUTOMATION_ADAPTER_H__

#include "Automation/AutomationReadModel.h"
#include "Automation/AutomationTypes.h"

#include <memory>

namespace Ailu
{
    namespace Editor
    {
        // Per-type automation adapter. Reads go through here so that the scene /
        // asset / reflection services stay decoupled from concrete types.
        // Resolution priority (per plan): specialized adapter -> component
        // descriptor adapter -> generic reflection adapter -> unsupported.
        // Writes are added in the command/set-property phase.
        class IAutomationTypeAdapter
        {
        public:
            virtual ~IAutomationTypeAdapter() = default;

            // Return the property schema for a stable type name.
            virtual AutomationResult Describe(StringView stable_type) const = 0;

            // Read a single property value from a resolved component/object instance.
            virtual AutomationResult ReadProperty(StringView stable_type, const void *instance, StringView property_path) const = 0;

            // Write a single property value. Default implementation rejects writes.
            virtual AutomationResult WriteProperty(StringView stable_type, void *instance, StringView property_path, const AutomationValue &value) const
            {
                return AutomationResult::Fail(AutomationErrors::kPropertyNotEditable,
                                              "property writes are not supported for this type");
            }
        };

        // Generic adapter over a ComponentDescriptor: handles any component type
        // that has a descriptor registered in ComponentDescriptorRegistry.
        class ComponentDescriptorAdapter final : public IAutomationTypeAdapter
        {
        public:
            AutomationResult Describe(StringView stable_type) const override;
            AutomationResult ReadProperty(StringView stable_type, const void *instance, StringView property_path) const override;
            AutomationResult WriteProperty(StringView stable_type, void *instance, StringView property_path, const AutomationValue &value) const override;
        };

        // Generic adapter over the reflection type system: handles reflected
        // object/asset types that are not covered by a specialized adapter.
        class GenericReflectionAdapter final : public IAutomationTypeAdapter
        {
        public:
            AutomationResult Describe(StringView stable_type) const override;
            AutomationResult ReadProperty(StringView stable_type, const void *instance, StringView property_path) const override;
            AutomationResult WriteProperty(StringView stable_type, void *instance, StringView property_path, const AutomationValue &value) const override;
        };

        class AutomationAdapterRegistry final : public NonCopyable
        {
        public:
            static AutomationAdapterRegistry &Get();

            void Initialize();

            // Resolve an adapter honoring: specialized -> descriptor -> reflection.
            const IAutomationTypeAdapter *Resolve(StringView stable_type) const;
            // Look up a type-specific specialized adapter only.
            const IAutomationTypeAdapter *Find(StringView stable_type) const;

        private:
            HashMap<String, Scope<IAutomationTypeAdapter>> _specialized;
            Scope<ComponentDescriptorAdapter> _component_adapter;
            Scope<GenericReflectionAdapter> _reflection_adapter;
        };

        // Enumerate the readable property paths of an adapter-resolved type via Describe.
        Vector<String> AutomationReadablePaths(const IAutomationTypeAdapter *adapter, StringView stable_type);
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_ADAPTER_H__
