#include "Automation/AutomationTypes.h"

namespace Ailu
{
    namespace Editor
    {
        bool AutomationValue::AsBool(bool default_value) const
        {
            if (const bool *value = std::get_if<bool>(&_value))
                return *value;
            return default_value;
        }

        i64 AutomationValue::AsInt(i64 default_value) const
        {
            if (const i64 *value = std::get_if<i64>(&_value))
                return *value;
            if (const u64 *value = std::get_if<u64>(&_value))
                return static_cast<i64>(*value);
            if (const f64 *value = std::get_if<f64>(&_value))
                return static_cast<i64>(*value);
            return default_value;
        }

        u64 AutomationValue::AsUInt(u64 default_value) const
        {
            if (const u64 *value = std::get_if<u64>(&_value))
                return *value;
            if (const i64 *value = std::get_if<i64>(&_value))
                return static_cast<u64>(*value);
            if (const f64 *value = std::get_if<f64>(&_value))
                return static_cast<u64>(*value);
            return default_value;
        }

        f64 AutomationValue::AsFloat(f64 default_value) const
        {
            if (const f64 *value = std::get_if<f64>(&_value))
                return *value;
            if (const i64 *value = std::get_if<i64>(&_value))
                return static_cast<f64>(*value);
            if (const u64 *value = std::get_if<u64>(&_value))
                return static_cast<f64>(*value);
            return default_value;
        }

        const String *AutomationValue::AsStringPtr() const
        {
            return std::get_if<String>(&_value);
        }

        String AutomationValue::AsString(const String &default_value) const
        {
            if (const String *value = std::get_if<String>(&_value))
                return *value;
            return default_value;
        }

        const AutomationArray &AutomationValue::AsArray() const
        {
            static const AutomationArray kEmptyArray;
            if (const auto *array = std::get_if<std::shared_ptr<AutomationArray>>(&_value))
            {
                if (*array)
                    return **array;
            }
            return kEmptyArray;
        }

        const AutomationObject &AutomationValue::AsObject() const
        {
            static const AutomationObject kEmptyObject;
            if (const auto *object = std::get_if<std::shared_ptr<AutomationObject>>(&_value))
            {
                if (*object)
                    return **object;
            }
            return kEmptyObject;
        }

        AutomationArray &AutomationValue::EnsureArray()
        {
            if (auto *array = std::get_if<std::shared_ptr<AutomationArray>>(&_value))
            {
                if (!*array)
                    *array = std::make_shared<AutomationArray>();
                return **array;
            }
            _value = std::make_shared<AutomationArray>();
            return *std::get<std::shared_ptr<AutomationArray>>(_value);
        }

        AutomationObject &AutomationValue::EnsureObject()
        {
            if (auto *object = std::get_if<std::shared_ptr<AutomationObject>>(&_value))
            {
                if (!*object)
                    *object = std::make_shared<AutomationObject>();
                return **object;
            }
            _value = std::make_shared<AutomationObject>();
            return *std::get<std::shared_ptr<AutomationObject>>(_value);
        }

        AutomationArray &AutomationValue::Array()
        {
            return EnsureArray();
        }

        AutomationObject &AutomationValue::Object()
        {
            return EnsureObject();
        }

        void AutomationValue::SetMember(const String &key, AutomationValue value)
        {
            Object().emplace(key, std::move(value));
        }

        void AutomationValue::Dump(String &out) const
        {
            if (IsNull())
            {
                out += "null";
                return;
            }
            if (IsBool())
            {
                out += AsBool() ? "true" : "false";
                return;
            }
            if (IsInt())
            {
                out += std::to_string(AsInt());
                return;
            }
            if (IsFloat())
            {
                out += std::to_string(AsFloat());
                return;
            }
            if (IsString())
            {
                out += '"';
                out += AsString();
                out += '"';
                return;
            }
            if (IsArray())
            {
                out += '[';
                bool first = true;
                for (const AutomationValue &item : AsArray())
                {
                    if (!first)
                        out += ", ";
                    first = false;
                    item.Dump(out);
                }
                out += ']';
                return;
            }
            if (IsObject())
            {
                out += '{';
                bool first = true;
                for (const auto &[key, value] : AsObject())
                {
                    if (!first)
                        out += ", ";
                    first = false;
                    out += key;
                    out += ':';
                    value.Dump(out);
                }
                out += '}';
                return;
            }
            out += '?';
        }

        String AutomationValue::Dump() const
        {
            String out;
            Dump(out);
            return out;
        }

        bool AutomationValue::operator==(const AutomationValue &other) const
        {
            if (IsNull())
                return other.IsNull();
            if (IsBool())
                return other.IsBool() && AsBool() == other.AsBool();
            if (IsString())
            {
                const String *a = AsStringPtr();
                const String *b = other.AsStringPtr();
                return a != nullptr && b != nullptr && *a == *b;
            }
            if (IsArray())
                return other.IsArray() && AsArray() == other.AsArray();
            if (IsObject())
                return other.IsObject() && AsObject() == other.AsObject();
            if (IsNumber() && other.IsNumber())
            {
                if (IsFloat() || other.IsFloat())
                    return AsFloat() == other.AsFloat();
                return AsInt() == other.AsInt() && AsUInt() == other.AsUInt();
            }
            return false;
        }
    }// namespace Editor
}// namespace Ailu
