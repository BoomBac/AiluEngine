#include "Automation/AutomationAdapter.h"

#include "Objects/Type.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            AutomationObject BuildComponentFieldSchema(const ComponentFieldDesc &field)
            {
                AutomationObject schema;
                schema.emplace("path", AutomationValue(field._path));
                schema.emplace("display_name", AutomationValue(field._display_name));
                schema.emplace("value_type", AutomationValue(ValueTypeToString(field._value_type)));
                schema.emplace("editable", AutomationValue(field._editable));
                schema.emplace("category", AutomationValue(field._category));
                if (!field._enum_values.empty())
                {
                    AutomationArray values;
                    for (const String &value : field._enum_values)
                        values.emplace_back(AutomationValue(value));
                    schema.emplace("enum_values", AutomationValue(std::move(values)));
                }
                return schema;
            }

            AutomationObject BuildReflectedPropertySchema(const PropertyInfo &prop)
            {
                AutomationObject schema;
                schema.emplace("path", AutomationValue(prop.Name()));
                const String display_name = prop.MetaInfo().GetString("DisplayName", String{});
                schema.emplace("display_name", AutomationValue(display_name.empty() ? prop.Name() : display_name));
                const EAutomationValueType value_type = ValueTypeFromProperty(prop);
                schema.emplace("value_type", AutomationValue(ValueTypeToString(value_type)));
                schema.emplace("editable", AutomationValue(true));
                schema.emplace("category", AutomationValue(prop.MetaInfo().GetString("Category", String{"General"})));
                if (value_type == EAutomationValueType::kEnum)
                {
                    if (const Type *type = prop.GetType())
                    {
                        if (const Enum *enum_type = dynamic_cast<const Enum *>(type))
                        {
                            AutomationArray values;
                            for (const String *name : enum_type->GetEnumNames())
                                values.emplace_back(AutomationValue(*name));
                            schema.emplace("enum_values", AutomationValue(std::move(values)));
                        }
                    }
                }
                return schema;
            }
        }// namespace

        // -----------------------------------------------------------------------
        // ComponentDescriptorAdapter
        // -----------------------------------------------------------------------
        AutomationResult ComponentDescriptorAdapter::Describe(StringView stable_type) const
        {
            const ComponentDescriptor *desc = ComponentDescriptorRegistry::Get().Find(stable_type);
            if (desc == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown component type");

            AutomationObject schema;
            schema.emplace("type", AutomationValue(String(stable_type)));
            schema.emplace("kind", AutomationValue("component"));
            schema.emplace("display_name", AutomationValue(desc->_display_name));
            AutomationArray properties;
            for (const ComponentFieldDesc &field : desc->_fields)
                properties.emplace_back(AutomationValue(BuildComponentFieldSchema(field)));
            schema.emplace("properties", AutomationValue(std::move(properties)));
            return AutomationResult::Ok(AutomationValue(std::move(schema)));
        }

        AutomationResult ComponentDescriptorAdapter::ReadProperty(StringView stable_type, const void *instance, StringView property_path) const
        {
            if (instance == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "component instance is null");
            const ComponentDescriptor *desc = ComponentDescriptorRegistry::Get().Find(stable_type);
            if (desc == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown component type");
            const auto it = desc->_readers.find(String(property_path));
            if (it == desc->_readers.end())
                return AutomationResult::Fail(AutomationErrors::kPropertyNotFound, "unknown property path");
            return AutomationResult::Ok(it->second(instance));
        }

        AutomationResult ComponentDescriptorAdapter::WriteProperty(StringView stable_type, void *instance, StringView property_path, const AutomationValue &value) const
        {
            if (instance == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "component instance is null");
            const ComponentDescriptor *desc = ComponentDescriptorRegistry::Get().Find(stable_type);
            if (desc == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown component type");
            const auto it = desc->_writers.find(String(property_path));
            if (it == desc->_writers.end())
                return AutomationResult::Fail(AutomationErrors::kPropertyNotEditable, "property is read-only or unknown");
            return it->second(instance, value)
                       ? AutomationResult::Ok(AutomationValue{})
                       : AutomationResult::Fail(AutomationErrors::kCommandFailed, "failed to write property");
        }

        // -----------------------------------------------------------------------
        // GenericReflectionAdapter
        // -----------------------------------------------------------------------
        AutomationResult GenericReflectionAdapter::Describe(StringView stable_type) const
        {
            const Type *type = Type::Find(DenormalizeTypeName(stable_type));
            if (type == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown type");

            AutomationObject schema;
            schema.emplace("type", AutomationValue(String(stable_type)));
            schema.emplace("kind", AutomationValue("object"));
            schema.emplace("display_name", AutomationValue(type->Name()));
            AutomationArray properties;
            for (const PropertyInfo &prop : type->GetProperties())
                properties.emplace_back(AutomationValue(BuildReflectedPropertySchema(prop)));
            schema.emplace("properties", AutomationValue(std::move(properties)));
            return AutomationResult::Ok(AutomationValue(std::move(schema)));
        }

        AutomationResult GenericReflectionAdapter::ReadProperty(StringView stable_type, const void *instance, StringView property_path) const
        {
            if (instance == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "object instance is null");
            const Type *type = Type::Find(DenormalizeTypeName(stable_type));
            if (type == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown type");
            for (const PropertyInfo &prop : type->GetProperties())
            {
                if (prop.Name() != property_path)
                    continue;
                AutomationValue value = ReadReflectedProperty(prop, const_cast<void *>(instance));
                if (value.IsNull())
                    return AutomationResult::Fail(AutomationErrors::kPropertyNotFound, "property value type is not supported");
                return AutomationResult::Ok(std::move(value));
            }
            return AutomationResult::Fail(AutomationErrors::kPropertyNotFound, "property not found");
        }

        AutomationResult GenericReflectionAdapter::WriteProperty(StringView stable_type, void *instance, StringView property_path, const AutomationValue &value) const
        {
            if (instance == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidArgument, "object instance is null");
            const Type *type = Type::Find(DenormalizeTypeName(stable_type));
            if (type == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInvalidType, "unknown type");
            for (const PropertyInfo &prop : type->GetProperties())
            {
                if (prop.Name() != property_path)
                    continue;
                if (!WriteReflectedProperty(prop, instance, value))
                    return AutomationResult::Fail(AutomationErrors::kPropertyNotEditable, "property value type is not supported");
                return AutomationResult::Ok(AutomationValue{});
            }
            return AutomationResult::Fail(AutomationErrors::kPropertyNotFound, "property not found");
        }

        // -----------------------------------------------------------------------
        // AutomationAdapterRegistry
        // -----------------------------------------------------------------------
        AutomationAdapterRegistry &AutomationAdapterRegistry::Get()
        {
            static AutomationAdapterRegistry s_instance;
            return s_instance;
        }

        void AutomationAdapterRegistry::Initialize()
        {
            if (_component_adapter != nullptr)
                return;
            _component_adapter = MakeScope<ComponentDescriptorAdapter>();
            _reflection_adapter = MakeScope<GenericReflectionAdapter>();
        }

        const IAutomationTypeAdapter *AutomationAdapterRegistry::Find(StringView stable_type) const
        {
            const auto it = _specialized.find(String(stable_type));
            return it != _specialized.end() ? it->second.get() : nullptr;
        }

        const IAutomationTypeAdapter *AutomationAdapterRegistry::Resolve(StringView stable_type) const
        {
            if (const IAutomationTypeAdapter *specialized = Find(stable_type))
                return specialized;
            if (ComponentDescriptorRegistry::Get().Find(stable_type) != nullptr)
                return _component_adapter.get();
            if (Type::Find(DenormalizeTypeName(stable_type)) != nullptr)
                return _reflection_adapter.get();
            return nullptr;
        }

        Vector<String> AutomationReadablePaths(const IAutomationTypeAdapter *adapter, StringView stable_type)
        {
            Vector<String> paths;
            if (adapter == nullptr)
                return paths;
            const AutomationResult schema = adapter->Describe(stable_type);
            if (!schema._success || !schema._data.IsObject())
                return paths;
            const auto it = schema._data.AsObject().find("properties");
            if (it == schema._data.AsObject().end() || !it->second.IsArray())
                return paths;
            for (const AutomationValue &entry : it->second.AsArray())
            {
                if (!entry.IsObject())
                    continue;
                const auto pit = entry.AsObject().find("path");
                if (pit != entry.AsObject().end())
                    paths.emplace_back(pit->second.AsString());
            }
            return paths;
        }
    }// namespace Editor
}// namespace Ailu
