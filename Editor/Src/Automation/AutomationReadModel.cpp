#include "Automation/AutomationReadModel.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        String NormalizeTypeName(StringView full_name)
        {
            String result(full_name);
            size_t pos = 0;
            while ((pos = result.find("::", pos)) != String::npos)
            {
                result.replace(pos, 2, ".");
                ++pos;
            }
            return result;
        }

        String DenormalizeTypeName(StringView normalized_name)
        {
            String result(normalized_name);
            size_t pos = 0;
            while ((pos = result.find('.', pos)) != String::npos)
            {
                result.replace(pos, 1, "::");
                pos += 2;
            }
            return result;
        }

        String ComponentStableTypeName(ECS::ComponentTypeId type_id)
        {
            StringView name = ECS::GetComponentStableName(type_id);
            return name.empty() ? std::format("Ailu.ECS.Component_{}", type_id) : String(name);
        }

        AutomationValue ToAutomationValue(bool value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(i32 value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(u32 value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(i64 value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(u64 value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(f32 value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(f64 value) { return AutomationValue(value); }
        AutomationValue ToAutomationValue(const String &value) { return AutomationValue(value); }

        AutomationValue ToAutomationValue(const Vector2f &value)
        {
            AutomationArray array;
            array.emplace_back(AutomationValue(static_cast<f64>(value.x)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.y)));
            return AutomationValue(std::move(array));
        }

        AutomationValue ToAutomationValue(const Vector3f &value)
        {
            AutomationArray array;
            array.emplace_back(AutomationValue(static_cast<f64>(value.x)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.y)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.z)));
            return AutomationValue(std::move(array));
        }

        AutomationValue ToAutomationValue(const Vector4f &value)
        {
            AutomationArray array;
            array.emplace_back(AutomationValue(static_cast<f64>(value.x)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.y)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.z)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.w)));
            return AutomationValue(std::move(array));
        }

        AutomationValue ToAutomationValue(const Quaternion &value)
        {
            AutomationArray array;
            array.emplace_back(AutomationValue(static_cast<f64>(value.x)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.y)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.z)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.w)));
            return AutomationValue(std::move(array));
        }

        AutomationValue ToAutomationValue(const Color &value)
        {
            AutomationObject object;
            object.emplace("type", AutomationValue("color"));
            object.emplace("space", AutomationValue("srgb"));
            AutomationArray array;
            array.emplace_back(AutomationValue(static_cast<f64>(value.r)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.g)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.b)));
            array.emplace_back(AutomationValue(static_cast<f64>(value.a)));
            object.emplace("value", AutomationValue(std::move(array)));
            return AutomationValue(std::move(object));
        }

        AutomationValue ToAutomationValue(const Guid &value)
        {
            return AutomationValue(value.ToString());
        }

        // ---- AutomationValue -> C++ conversions ----
        bool ToBool(const AutomationValue &value, bool default_value)
        {
            return value.IsBool() ? value.AsBool() : default_value;
        }

        i64 ToInt(const AutomationValue &value, i64 default_value)
        {
            return value.IsNumber() ? value.AsInt() : default_value;
        }

        f32 ToFloat(const AutomationValue &value, f32 default_value)
        {
            return value.IsNumber() ? static_cast<f32>(value.AsFloat()) : default_value;
        }

        f64 ToDouble(const AutomationValue &value, f64 default_value)
        {
            return value.IsNumber() ? value.AsFloat() : default_value;
        }

        String ToString(const AutomationValue &value, const String &default_value)
        {
            return value.IsString() ? value.AsString() : default_value;
        }

        Vector2f ToVector2f(const AutomationValue &value, const Vector2f &default_value)
        {
            if (!value.IsArray())
                return default_value;
            const AutomationArray &array = value.AsArray();
            const auto at = [&array](size_t index, f32 fallback) -> f32
            {
                return index < array.size() && array[index].IsNumber() ? static_cast<f32>(array[index].AsFloat()) : fallback;
            };
            return Vector2f(at(0, default_value.x), at(1, default_value.y));
        }

        Vector3f ToVector3f(const AutomationValue &value, const Vector3f &default_value)
        {
            if (!value.IsArray())
                return default_value;
            const AutomationArray &array = value.AsArray();
            const auto at = [&array](size_t index, f32 fallback) -> f32
            {
                return index < array.size() && array[index].IsNumber() ? static_cast<f32>(array[index].AsFloat()) : fallback;
            };
            return Vector3f(at(0, default_value.x), at(1, default_value.y), at(2, default_value.z));
        }

        Vector4f ToVector4f(const AutomationValue &value, const Vector4f &default_value)
        {
            if (!value.IsArray())
                return default_value;
            const AutomationArray &array = value.AsArray();
            const auto at = [&array](size_t index, f32 fallback) -> f32
            {
                return index < array.size() && array[index].IsNumber() ? static_cast<f32>(array[index].AsFloat()) : fallback;
            };
            return Vector4f(at(0, default_value.x), at(1, default_value.y), at(2, default_value.z), at(3, default_value.w));
        }

        Quaternion ToQuaternion(const AutomationValue &value, const Quaternion &default_value)
        {
            if (!value.IsArray())
                return default_value;
            const AutomationArray &array = value.AsArray();
            const auto at = [&array](size_t index, f32 fallback) -> f32
            {
                return index < array.size() && array[index].IsNumber() ? static_cast<f32>(array[index].AsFloat()) : fallback;
            };
            return Quaternion(at(0, default_value.x), at(1, default_value.y), at(2, default_value.z), at(3, default_value.w));
        }

        Color ToColor(const AutomationValue &value, const Color &default_value)
        {
            if (value.IsObject())
            {
                const AutomationObject &object = value.AsObject();
                const auto value_it = object.find("value");
                if (value_it != object.end() && value_it->second.IsArray())
                {
                    const AutomationArray &array = value_it->second.AsArray();
                    const auto at = [&array](size_t index, f32 fallback) -> f32
                    {
                        return index < array.size() && array[index].IsNumber() ? static_cast<f32>(array[index].AsFloat()) : fallback;
                    };
                    return Color(at(0, default_value.r), at(1, default_value.g), at(2, default_value.b), at(3, default_value.a));
                }
                return default_value;
            }
            if (value.IsArray())
            {
                const AutomationArray &array = value.AsArray();
                const auto at = [&array](size_t index, f32 fallback) -> f32
                {
                    return index < array.size() && array[index].IsNumber() ? static_cast<f32>(array[index].AsFloat()) : fallback;
                };
                return Color(at(0, default_value.r), at(1, default_value.g), at(2, default_value.b), at(3, default_value.a));
            }
            return default_value;
        }

        // -----------------------------------------------------------------------
        // Component descriptors
        // -----------------------------------------------------------------------
        ComponentDescriptorRegistry &ComponentDescriptorRegistry::Get()
        {
            static ComponentDescriptorRegistry s_instance;
            return s_instance;
        }

        void ComponentDescriptorRegistry::Register(ComponentDescriptor &&desc)
        {
            if (desc._stable_type.empty() || Find(desc._stable_type) != nullptr)
                return;
            _indices.emplace(desc._stable_type, _descriptors.size());
            _descriptors.emplace_back(std::move(desc));
        }

        const ComponentDescriptor *ComponentDescriptorRegistry::Find(StringView stable_type) const
        {
            const auto it = _indices.find(String(stable_type));
            if (it == _indices.end())
                return nullptr;
            return &_descriptors[it->second];
        }

        namespace
        {
            template<typename TComp>
            void AddField(ComponentDescriptor &desc, StringView path, StringView display_name,
                          EAutomationValueType type, StringView category,
                          const std::function<AutomationValue(const TComp &)> &reader,
                          const std::function<bool(TComp &, const AutomationValue &)> &writer = nullptr,
                          Vector<String> enum_values = {})
            {
                ComponentFieldDesc field;
                field._path = path;
                field._display_name = display_name;
                field._value_type = type;
                field._category = category;
                field._enum_values = std::move(enum_values);
                field._editable = writer != nullptr;
                desc._fields.push_back(std::move(field));
                const String key(path);
                desc._readers.emplace(key, [reader](const void *instance) -> AutomationValue
                {
                    return reader(*static_cast<const TComp *>(instance));
                });
                if (writer != nullptr)
                {
                    desc._writers.emplace(key, [writer](void *instance, const AutomationValue &value) -> bool
                    {
                        return writer(*static_cast<TComp *>(instance), value);
                    });
                }
            }
        }// namespace

        void RegisterDefaultComponentDescriptors()
        {
            auto &registry = ComponentDescriptorRegistry::Get();
            if (registry.Find("Ailu.ECS.TransformComponent") != nullptr)
                return;

            {
                ComponentDescriptor desc;
                desc._stable_type = "Ailu.ECS.TransformComponent";
                desc._display_name = "Transform";
                AddField<ECS::TransformComponent>(desc, "_local_transform._position", "Position", EAutomationValueType::kVector, "Transform",
                                                  [](const ECS::TransformComponent &c) { return ToAutomationValue(c._local_transform._position); },
                                                  [](ECS::TransformComponent &c, const AutomationValue &v) { c.SetLocalPosition(ToVector3f(v)); return true; });
                AddField<ECS::TransformComponent>(desc, "_local_transform._rotation", "Rotation", EAutomationValueType::kQuaternion, "Transform",
                                                  [](const ECS::TransformComponent &c) { return ToAutomationValue(c._local_transform._rotation); },
                                                  [](ECS::TransformComponent &c, const AutomationValue &v) { c.SetLocalRotation(ToQuaternion(v)); return true; });
                AddField<ECS::TransformComponent>(desc, "_local_transform._scale", "Scale", EAutomationValueType::kVector, "Transform",
                                                  [](const ECS::TransformComponent &c) { return ToAutomationValue(c._local_transform._scale); },
                                                  [](ECS::TransformComponent &c, const AutomationValue &v) { c.SetLocalScale(ToVector3f(v)); return true; });
                registry.Register(std::move(desc));
            }

            {
                ComponentDescriptor desc;
                desc._stable_type = "Ailu.ECS.TagComponent";
                desc._display_name = "Tag";
                AddField<ECS::TagComponent>(desc, "_name", "Name", EAutomationValueType::kString, "Core",
                                            [](const ECS::TagComponent &c) { return ToAutomationValue(c._name); },
                                            [](ECS::TagComponent &c, const AutomationValue &v) { c._name = ToString(v); return true; });
                registry.Register(std::move(desc));
            }

            {
                ComponentDescriptor desc;
                desc._stable_type = "Ailu.ECS.LightComponent";
                desc._display_name = "Light";
                AddField<ECS::LightComponent>(desc, "_type", "Light Type", EAutomationValueType::kEnum, "Light",
                                              [](const ECS::LightComponent &c) { return AutomationValue(EnumValueName(c._type, "Ailu::ECS::ELightType")); },
                                              [](ECS::LightComponent &c, const AutomationValue &v)
                                              {
                                                  const String name = ToString(v);
                                                  if (name == "kPoint") c._type = ECS::ELightType::kPoint;
                                                  else if (name == "kSpot") c._type = ECS::ELightType::kSpot;
                                                  else if (name == "kArea") c._type = ECS::ELightType::kArea;
                                                  else c._type = ECS::ELightType::kDirectional;
                                                  return true;
                                              },
                                              {"kDirectional", "kPoint", "kSpot", "kArea"});
                AddField<ECS::LightComponent>(desc, "_light._light_color", "Light Color", EAutomationValueType::kColor, "Light",
                                              [](const ECS::LightComponent &c) { return ToAutomationValue(c._light._light_color); },
                                              [](ECS::LightComponent &c, const AutomationValue &v) { c._light._light_color = ToColor(v); return true; });
                AddField<ECS::LightComponent>(desc, "_shadow._is_cast_shadow", "Cast Shadow", EAutomationValueType::kBool, "Light",
                                              [](const ECS::LightComponent &c) { return ToAutomationValue(c._shadow._is_cast_shadow); },
                                              [](ECS::LightComponent &c, const AutomationValue &v) { c._shadow._is_cast_shadow = ToBool(v); return true; });
                registry.Register(std::move(desc));
            }

            {
                ComponentDescriptor desc;
                desc._stable_type = "Ailu.ECS.CCamera";
                desc._display_name = "Camera";
                AddField<ECS::CCamera>(desc, "_camera._fov_h", "FOV", EAutomationValueType::kFloat, "Camera",
                                       [](const ECS::CCamera &c) { return ToAutomationValue(c._camera.FovH()); },
                                       [](ECS::CCamera &c, const AutomationValue &v) { c._camera.FovH(ToFloat(v)); return true; });
                AddField<ECS::CCamera>(desc, "_camera._near_clip", "Near", EAutomationValueType::kFloat, "Camera",
                                       [](const ECS::CCamera &c) { return ToAutomationValue(c._camera.Near()); },
                                       [](ECS::CCamera &c, const AutomationValue &v) { c._camera.Near(ToFloat(v)); return true; });
                AddField<ECS::CCamera>(desc, "_camera._far_clip", "Far", EAutomationValueType::kFloat, "Camera",
                                       [](const ECS::CCamera &c) { return ToAutomationValue(c._camera.Far()); },
                                       [](ECS::CCamera &c, const AutomationValue &v) { c._camera.Far(ToFloat(v)); return true; });
                registry.Register(std::move(desc));
            }

            {
                ComponentDescriptor desc;
                desc._stable_type = "Ailu.ECS.PersistentIdComponent";
                desc._display_name = "Persistent ID";
                AddField<ECS::PersistentIdComponent>(desc, "_guid", "Guid", EAutomationValueType::kGuid, "Core",
                                                     [](const ECS::PersistentIdComponent &c) { return ToAutomationValue(c._guid); });
                registry.Register(std::move(desc));
            }
        }

        AutomationValue ReadComponentField(const void *instance, StringView stable_type, StringView path)
        {
            const ComponentDescriptor *desc = ComponentDescriptorRegistry::Get().Find(stable_type);
            if (desc == nullptr)
                return AutomationValue{};
            const auto it = desc->_readers.find(String(path));
            return it != desc->_readers.end() ? it->second(instance) : AutomationValue{};
        }

        bool WriteComponentField(void *instance, StringView stable_type, StringView path, const AutomationValue &value)
        {
            if (instance == nullptr)
                return false;
            const ComponentDescriptor *desc = ComponentDescriptorRegistry::Get().Find(stable_type);
            if (desc == nullptr)
                return false;
            const auto it = desc->_writers.find(String(path));
            if (it == desc->_writers.end())
                return false;
            return it->second(instance, value);
        }

        EAutomationValueType ValueTypeFromProperty(const PropertyInfo &prop)
        {
            const String &type_name = prop.TypeName();
            if (type_name == "bool")
                return EAutomationValueType::kBool;
            if (type_name == "f32" || type_name == "float" || type_name == "f64" || type_name == "double")
                return EAutomationValueType::kFloat;
            if (type_name == "i32" || type_name == "int" || type_name == "u32" || type_name == "i64" || type_name == "u64")
                return EAutomationValueType::kInteger;
            if (type_name == "String" || type_name == "std::string")
                return EAutomationValueType::kString;
            if (type_name == "Guid")
                return EAutomationValueType::kGuid;
            if (type_name == "Color")
                return EAutomationValueType::kColor;
            if (type_name == "Quaternion")
                return EAutomationValueType::kQuaternion;
            if (type_name == "Vector2f" || type_name == "Vector3f" || type_name == "Vector4f")
                return EAutomationValueType::kVector;
            if (const Type *type = prop.GetType())
            {
                if (dynamic_cast<const Enum *>(type) != nullptr)
                    return EAutomationValueType::kEnum;
            }
            return EAutomationValueType::kString;
        }

        const char *ValueTypeToString(EAutomationValueType value_type)
        {
            switch (value_type)
            {
                case EAutomationValueType::kBool: return "bool";
                case EAutomationValueType::kInteger: return "integer";
                case EAutomationValueType::kFloat: return "float";
                case EAutomationValueType::kString: return "string";
                case EAutomationValueType::kGuid: return "guid";
                case EAutomationValueType::kEnum: return "enum";
                case EAutomationValueType::kVector: return "vector";
                case EAutomationValueType::kQuaternion: return "quaternion";
                case EAutomationValueType::kColor: return "color";
                case EAutomationValueType::kArray: return "array";
                case EAutomationValueType::kObject: return "object";
                case EAutomationValueType::kEntityReference: return "entity_ref";
                case EAutomationValueType::kAssetReference: return "asset_ref";
                case EAutomationValueType::kObjectReference: return "object_ref";
                case EAutomationValueType::kNull: return "null";
            }
            return "string";
        }

        AutomationValue ReadReflectedProperty(const PropertyInfo &prop, void *instance)
        {
            const String &type_name = prop.TypeName();
            if (type_name == "bool")
                return ToAutomationValue(prop.Get<bool>(instance));
            if (type_name == "i32" || type_name == "int")
                return ToAutomationValue(prop.Get<i32>(instance));
            if (type_name == "u32" || type_name == "uint" || type_name == "unsigned int")
                return ToAutomationValue(prop.Get<u32>(instance));
            if (type_name == "i64")
                return ToAutomationValue(prop.Get<i64>(instance));
            if (type_name == "u64")
                return ToAutomationValue(prop.Get<u64>(instance));
            if (type_name == "f32" || type_name == "float")
                return ToAutomationValue(prop.Get<f32>(instance));
            if (type_name == "f64" || type_name == "double")
                return ToAutomationValue(prop.Get<f64>(instance));
            if (type_name == "String" || type_name == "std::string")
                return ToAutomationValue(prop.Get<String>(instance));
            if (type_name == "Vector2f")
                return ToAutomationValue(prop.Get<Vector2f>(instance));
            if (type_name == "Vector3f")
                return ToAutomationValue(prop.Get<Vector3f>(instance));
            if (type_name == "Vector4f")
                return ToAutomationValue(prop.Get<Vector4f>(instance));
            if (type_name == "Color")
                return ToAutomationValue(prop.Get<Color>(instance));
            if (type_name == "Quaternion")
                return ToAutomationValue(prop.Get<Quaternion>(instance));
            if (const Type *type = prop.GetType())
            {
                if (const Enum *enum_type = dynamic_cast<const Enum *>(type))
                {
                    return AutomationValue(enum_type->GetNameByIndex(static_cast<u32>(prop.Get<i32>(instance))));
                }
            }
            return AutomationValue{};
        }

        bool WriteReflectedProperty(const PropertyInfo &prop, void *instance, const AutomationValue &value)
        {
            const String &type_name = prop.TypeName();
            if (type_name == "bool")
            {
                prop.Set<bool>(instance, ToBool(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "i32" || type_name == "int")
            {
                prop.Set<i32>(instance, static_cast<i32>(ToInt(value)), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "u32" || type_name == "uint" || type_name == "unsigned int")
            {
                prop.Set<u32>(instance, static_cast<u32>(ToInt(value)), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "i64")
            {
                prop.Set<i64>(instance, ToInt(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "u64")
            {
                prop.Set<u64>(instance, static_cast<u64>(ToInt(value)), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "f32" || type_name == "float")
            {
                prop.Set<f32>(instance, ToFloat(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "f64" || type_name == "double")
            {
                prop.Set<f64>(instance, ToDouble(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "String" || type_name == "std::string")
            {
                prop.Set<String>(instance, ToString(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "Vector2f")
            {
                prop.Set<Vector2f>(instance, ToVector2f(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "Vector3f")
            {
                prop.Set<Vector3f>(instance, ToVector3f(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "Vector4f")
            {
                prop.Set<Vector4f>(instance, ToVector4f(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "Color")
            {
                prop.Set<Color>(instance, ToColor(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (type_name == "Quaternion")
            {
                prop.Set<Quaternion>(instance, ToQuaternion(value), PropertyInfo::EPropertyChangeSource::kAutomation);
                return true;
            }
            if (const Type *type = prop.GetType())
            {
                if (const Enum *enum_type = dynamic_cast<const Enum *>(type))
                {
                    const String name = ToString(value);
                    const i32 index = enum_type->GetIndexByName(name);
                    if (index >= 0)
                        prop.Set<i32>(instance, index, PropertyInfo::EPropertyChangeSource::kAutomation);
                    else
                        prop.Set<i32>(instance, static_cast<i32>(ToInt(value)), PropertyInfo::EPropertyChangeSource::kAutomation);
                    return true;
                }
            }
            return false;
        }
    }// namespace Editor
}// namespace Ailu
