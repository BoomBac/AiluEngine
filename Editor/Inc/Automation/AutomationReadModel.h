#pragma once
#ifndef __AUTOMATION_READ_MODEL_H__
#define __AUTOMATION_READ_MODEL_H__

#include "Automation/AutomationTypes.h"
#include "Framework/Common/NonCopyable.h"
#include "Framework/Math/Color.h"
#include "Framework/Math/Guid.h"
#include "Framework/Math/Quaternion.h"
#include "Framework/Math/Transform.h"
#include "Framework/Math/Vector.hpp"
#include "Objects/Type.h"
#include "Scene/Component.h"

#include <functional>

namespace Ailu
{
    namespace Editor
    {
        // ---- Type name helpers ----
        // "Ailu::Render::Material" -> "Ailu.Render.Material"
        String NormalizeTypeName(StringView full_name);
        // Reverse: "Ailu.Render.Material" -> "Ailu::Render::Material"
        String DenormalizeTypeName(StringView normalized_name);
        // Runtime ComponentTypeId -> stable dot-separated component type name.
        String ComponentStableTypeName(ECS::ComponentTypeId type_id);

        // ---- C++ value -> AutomationValue conversions ----
        AutomationValue ToAutomationValue(bool value);
        AutomationValue ToAutomationValue(i32 value);
        AutomationValue ToAutomationValue(u32 value);
        AutomationValue ToAutomationValue(i64 value);
        AutomationValue ToAutomationValue(u64 value);
        AutomationValue ToAutomationValue(f32 value);
        AutomationValue ToAutomationValue(f64 value);
        AutomationValue ToAutomationValue(const String &value);
        AutomationValue ToAutomationValue(const Vector2f &value);
        AutomationValue ToAutomationValue(const Vector3f &value);
        AutomationValue ToAutomationValue(const Vector4f &value);
        AutomationValue ToAutomationValue(const Quaternion &value);
        AutomationValue ToAutomationValue(const Color &value);
        AutomationValue ToAutomationValue(const Guid &value);

        // Enum value name string (via Enum reflection); falls back to the numeric string.
        template<typename E>
        String EnumValueName(E value, StringView enum_full_name)
        {
            const Enum *enum_type = Enum::GetEnumByName(String(enum_full_name));
            if (enum_type != nullptr)
                return enum_type->GetNameByEnum(value);
            return std::to_string(static_cast<i64>(value));
        }

        // ---- AutomationValue -> C++ conversions (write path) ----
        bool ToBool(const AutomationValue &value, bool default_value = false);
        i64 ToInt(const AutomationValue &value, i64 default_value = 0);
        f32 ToFloat(const AutomationValue &value, f32 default_value = 0.0f);
        f64 ToDouble(const AutomationValue &value, f64 default_value = 0.0);
        String ToString(const AutomationValue &value, const String &default_value = {});
        Vector2f ToVector2f(const AutomationValue &value, const Vector2f &default_value = Vector2f::kZero);
        Vector3f ToVector3f(const AutomationValue &value, const Vector3f &default_value = Vector3f::kZero);
        Vector4f ToVector4f(const AutomationValue &value, const Vector4f &default_value = Vector4f::kZero);
        Quaternion ToQuaternion(const AutomationValue &value, const Quaternion &default_value = Quaternion::Identity());
        Color ToColor(const AutomationValue &value, const Color &default_value = Colors::kWhite);

        // ---- Component read/write descriptors ----
        // ECS components are not reflected types, so their fields are described
        // here explicitly. Read-only fields have no writer.
        using ComponentFieldReader = std::function<AutomationValue(const void *instance)>;
        using ComponentFieldWriter = std::function<bool(void *instance, const AutomationValue &value)>;

        struct ComponentFieldDesc
        {
            String _path;
            String _display_name;
            EAutomationValueType _value_type = EAutomationValueType::kNull;
            String _category;
            Vector<String> _enum_values;
            bool _editable = true;
        };

        struct ComponentDescriptor
        {
            String _stable_type;
            String _display_name;
            Vector<ComponentFieldDesc> _fields;
            HashMap<String, ComponentFieldReader> _readers;
            HashMap<String, ComponentFieldWriter> _writers;
        };

        class ComponentDescriptorRegistry final : public NonCopyable
        {
        public:
            static ComponentDescriptorRegistry &Get();

            void Register(ComponentDescriptor &&desc);
            const ComponentDescriptor *Find(StringView stable_type) const;
            const Vector<ComponentDescriptor> &Descriptors() const { return _descriptors; }

        private:
            Vector<ComponentDescriptor> _descriptors;
            HashMap<String, u64> _indices;
        };

        // Register descriptors for the common editable components (read-only set, phase 2).
        void RegisterDefaultComponentDescriptors();

        // Read a component field by path; Null AutomationValue when unknown.
        AutomationValue ReadComponentField(const void *instance, StringView stable_type, StringView path);
        // Write a component field by path. Returns false when the field is unknown or read-only.
        bool WriteComponentField(void *instance, StringView stable_type, StringView path, const AutomationValue &value);

        // Read a reflected property value from an instance (assets / Objects).
        // Unsupported value types yield a Null AutomationValue.
        AutomationValue ReadReflectedProperty(const PropertyInfo &prop, void *instance);
        // Write a reflected property value from an AutomationValue. Returns false
        // when the property type is not supported.
        bool WriteReflectedProperty(const PropertyInfo &prop, void *instance, const AutomationValue &value);

        // Map a reflected property's type name to a logical automation value type.
        EAutomationValueType ValueTypeFromProperty(const PropertyInfo &prop);
        const char *ValueTypeToString(EAutomationValueType value_type);
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_READ_MODEL_H__
