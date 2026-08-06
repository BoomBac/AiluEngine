#pragma once
#ifndef __AUTOMATION_TYPES_H__
#define __AUTOMATION_TYPES_H__

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Core/Containers/Vector.h"
#include "Framework/Core/Containers/Map.h"

#include <algorithm>
#include <memory>
#include <type_traits>
#include <variant>

namespace Ailu
{
    namespace Editor
    {
        class AutomationValue;

        using AutomationArray = Vector<AutomationValue>;
        using AutomationObject = HashMap<String, AutomationValue>;

        // Logical value type used in schemas and in JSON transport. The physical
        // storage of AutomationValue keeps only primitive alternatives; richer
        // values (color / vector / guid / enum / reference) are carried as tagged
        // objects or strings, matching the JSON representation defined in the plan.
        enum class EAutomationValueType
        {
            kNull,
            kBool,
            kInteger,
            kFloat,
            kString,
            kGuid,
            kEnum,
            kVector,
            kQuaternion,
            kColor,
            kArray,
            kObject,
            kEntityReference,
            kAssetReference,
            kObjectReference,
        };

        class AutomationValue
        {
        public:
            using Storage = std::variant<
                std::nullptr_t,
                bool,
                i64,
                u64,
                f64,
                String,
                std::shared_ptr<AutomationArray>,
                std::shared_ptr<AutomationObject>>;

            AutomationValue() = default;
            explicit AutomationValue(std::nullptr_t) {}
            explicit AutomationValue(bool value) : _value(value) {}
            explicit AutomationValue(i32 value) : _value(static_cast<i64>(value)) {}
            explicit AutomationValue(u32 value) : _value(static_cast<u64>(value)) {}
            explicit AutomationValue(i64 value) : _value(value) {}
            explicit AutomationValue(u64 value) : _value(value) {}
            explicit AutomationValue(f32 value) : _value(static_cast<f64>(value)) {}
            explicit AutomationValue(f64 value) : _value(value) {}
            explicit AutomationValue(const char *value) : _value(String(value)) {}
            explicit AutomationValue(const String &value) : _value(value) {}
            explicit AutomationValue(String &&value) : _value(std::move(value)) {}

            template<typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
            explicit AutomationValue(E value) : _value(static_cast<i64>(value))
            {
            }

            explicit AutomationValue(const AutomationArray &value) : _value(std::make_shared<AutomationArray>(value)) {}
            explicit AutomationValue(AutomationArray &&value) : _value(std::make_shared<AutomationArray>(std::move(value))) {}
            explicit AutomationValue(const AutomationObject &value) : _value(std::make_shared<AutomationObject>(value)) {}
            explicit AutomationValue(AutomationObject &&value) : _value(std::make_shared<AutomationObject>(std::move(value))) {}

            static AutomationValue MakeArray() { return AutomationValue(AutomationArray{}); }
            static AutomationValue MakeObject() { return AutomationValue(AutomationObject{}); }

            // ---- Type queries ----
            bool IsNull() const { return std::holds_alternative<std::nullptr_t>(_value); }
            bool IsBool() const { return std::holds_alternative<bool>(_value); }
            bool IsInt() const { return std::holds_alternative<i64>(_value) || std::holds_alternative<u64>(_value); }
            bool IsFloat() const { return std::holds_alternative<f64>(_value); }
            bool IsNumber() const { return IsInt() || IsFloat(); }
            bool IsString() const { return std::holds_alternative<String>(_value); }
            bool IsArray() const { return std::holds_alternative<std::shared_ptr<AutomationArray>>(_value); }
            bool IsObject() const { return std::holds_alternative<std::shared_ptr<AutomationObject>>(_value); }

            // ---- Accessors (missing / mismatched types fall back to defaults) ----
            bool AsBool(bool default_value = false) const;
            i64 AsInt(i64 default_value = 0) const;
            u64 AsUInt(u64 default_value = 0u) const;
            f64 AsFloat(f64 default_value = 0.0) const;
            const String *AsStringPtr() const;
            String AsString(const String &default_value = {}) const;
            const AutomationArray &AsArray() const;
            const AutomationObject &AsObject() const;

            // ---- Mutating helpers ----
            AutomationArray &Array();
            AutomationObject &Object();
            AutomationArray &EnsureArray();
            AutomationObject &EnsureObject();
            void SetMember(const String &key, AutomationValue value);

            size_t Index() const { return _value.index(); }
            const Storage &StorageRef() const { return _value; }

            // Compact single-line debug dump (ASCII only).
            void Dump(String &out) const;
            String Dump() const;

            bool operator==(const AutomationValue &other) const;
            bool operator!=(const AutomationValue &other) const { return !(*this == other); }

        private:
            Storage _value{nullptr};
        };

        // Fixed error codes shared by every automation domain.
        namespace AutomationErrors
        {
            inline constexpr const char *kEditorNotReady = "editor_not_ready";
            inline constexpr const char *kEditorShuttingDown = "editor_shutting_down";
            inline constexpr const char *kEditorInPlayMode = "editor_in_play_mode";
            inline constexpr const char *kProjectNotOpen = "project_not_open";
            inline constexpr const char *kSceneNotOpen = "scene_not_open";
            inline constexpr const char *kSceneNotFound = "scene_not_found";
            inline constexpr const char *kSceneRevisionConflict = "scene_revision_conflict";
            inline constexpr const char *kTargetNotFound = "target_not_found";
            inline constexpr const char *kEntityNotFound = "entity_not_found";
            inline constexpr const char *kAssetNotFound = "asset_not_found";
            inline constexpr const char *kComponentNotFound = "component_not_found";
            inline constexpr const char *kPropertyNotFound = "property_not_found";
            inline constexpr const char *kPropertyNotEditable = "property_not_editable";
            inline constexpr const char *kActionNotFound = "action_not_found";
            inline constexpr const char *kInvalidArgument = "invalid_argument";
            inline constexpr const char *kInvalidGuid = "invalid_guid";
            inline constexpr const char *kInvalidType = "invalid_type";
            inline constexpr const char *kAmbiguousTarget = "ambiguous_target";
            inline constexpr const char *kPermissionDenied = "permission_denied";
            inline constexpr const char *kCommandFailed = "command_failed";
            inline constexpr const char *kTransactionFailed = "transaction_failed";
            inline constexpr const char *kTransportError = "transport_error";
            inline constexpr const char *kInternalError = "internal_error";
        }// namespace AutomationErrors

        struct AutomationError
        {
            String _code;
            String _message;
            AutomationObject _details;

            static AutomationError Make(String code, String message)
            {
                AutomationError error;
                error._code = std::move(code);
                error._message = std::move(message);
                return error;
            }
        };

        struct AutomationResult
        {
            bool _success = false;
            AutomationValue _data;
            AutomationError _error;

            static AutomationResult Ok(AutomationValue data = AutomationValue{})
            {
                AutomationResult result;
                result._success = true;
                result._data = std::move(data);
                return result;
            }
            static AutomationResult Fail(AutomationError error)
            {
                AutomationResult result;
                result._success = false;
                result._error = std::move(error);
                return result;
            }
            static AutomationResult Fail(String code, String message)
            {
                return Fail(AutomationError::Make(std::move(code), std::move(message)));
            }
        };

        struct AutomationRequestContext
        {
            String _caller;
            bool _allow_write = false;
            bool _allow_destructive = false;
            bool _interactive = true;
        };

        struct AutomationRequest
        {
            u64 _request_id = 0u;
            String _method;
            AutomationObject _arguments;
            AutomationRequestContext _context;
        };

        struct AutomationParamDesc
        {
            String _name;
            String _description;
            EAutomationValueType _type = EAutomationValueType::kString;
            bool _required = false;
            bool _is_array = false;
            Vector<String> _enum_values;
        };

        struct AutomationSchema
        {
            Vector<AutomationParamDesc> _params;

            const AutomationParamDesc *Find(StringView name) const
            {
                const auto it = std::find_if(_params.begin(), _params.end(),
                                             [&](const AutomationParamDesc &p) { return p._name == name; });
                return it != _params.end() ? &*it : nullptr;
            }

            void AddParam(String name, String description, EAutomationValueType type,
                          bool required = false, bool is_array = false)
            {
                AutomationParamDesc param;
                param._name = std::move(name);
                param._description = std::move(description);
                param._type = type;
                param._required = required;
                param._is_array = is_array;
                _params.push_back(std::move(param));
            }
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_TYPES_H__
