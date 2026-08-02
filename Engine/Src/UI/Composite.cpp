#include "UI/Composite.h"
#include "UI/Basic.h"
#include "UI/Container.h"

#include <type_traits>

namespace Ailu
{
    namespace UI
    {
        #pragma region VectorFieldHelpers
        static f32 GetLabelFillRate(u16 length)
        {
            // Label 占比随分量数递减
            // Vector 越长，Label 越应该让位给输入区
            switch (length)
            {
                case 1:
                    return 3.0f;// Float
                case 2:
                    return 2.8f;// Vector2
                case 3:
                    return 2.5f;// Vector3（最常用）
                case 4:
                    return 2.2f;// Vector4
                default:
                    return 2.0f;
            }
        }

        static f32 GetInputFillRate(u16 length)
        {
            // 每个输入框的宽度
            // 保证数值输入不拥挤
            switch (length)
            {
                case 1:
                    return 3.0f;
                case 2:
                    return 1.6f;
                case 3:
                    return 1.5f;
                case 4:
                    return 1.3f;
                default:
                    return 1.2f;
            }
        }

        template<typename T>
        static String FormatNumericFieldValue(T value)
        {
            if constexpr (std::is_floating_point_v<T>)
                return std::format("{:.2f}", value);
            else
                return std::to_string(value);
        }

        template<typename T>
        static std::optional<T> ParseNumericFieldValue(const String &content)
        {
            if constexpr (std::is_floating_point_v<T>)
                return StringUtils::ParseFloat(content);
            else
                return StringUtils::ParseInt32(content);
        }

        template<typename T>
        struct VectorTraits;
        template<>
        struct VectorTraits<Vector2f>
        {
            using Scalar = float;
            static constexpr int kDimension = 2;

            static float Get(const Vector2f &v, int i)
            {
                return i == 0 ? v.x : v.y;
            }

            static void Set(Vector2f &v, int i, float value)
            {
                if (i == 0) v.x = value;
                else
                    v.y = value;
            }
        };
        template<>
        struct VectorTraits<Vector3f>
        {
            using Scalar = float;
            static constexpr int kDimension = 3;

            static float Get(const Vector3f &v, int i)
            {
                return i == 0 ? v.x : (i == 1 ? v.y : v.z);
            }

            static void Set(Vector3f &v, int i, float value)
            {
                if (i == 0) v.x = value;
                else if (i == 1)
                    v.y = value;
                else
                    v.z = value;
            }
        };
        template<>
        struct VectorTraits<Vector4f>
        {
            using Scalar = float;
            static constexpr int kDimension = 4;

            static float Get(const Vector4f &v, int i)
            {
                return i == 0 ? v.x : (i == 1 ? v.y : (i == 2 ? v.z : v.w));
            }

            static void Set(Vector4f &v, int i, float value)
            {
                if (i == 0) v.x = value;
                else if (i == 1)
                    v.y = value;
                else if (i == 2)
                    v.z = value;
                else
                    v.w = value;
            }
        };

        template<>
        struct VectorTraits<Vector2Int>
        {
            using Scalar = int;
            static constexpr int kDimension = 2;

            static int Get(const Vector2Int &v, int i)
            {
                return i == 0 ? v.x : v.y;
            }

            static void Set(Vector2Int &v, int i, int value)
            {
                if (i == 0) v.x = value;
                else
                    v.y = value;
            }
        };
        template<>
        struct VectorTraits<Vector3Int>
        {
            using Scalar = int;
            static constexpr int kDimension = 3;

            static int Get(const Vector3Int &v, int i)
            {
                return i == 0 ? v.x : (i == 1 ? v.y : v.z);
            }

            static void Set(Vector3Int &v, int i, int value)
            {
                if (i == 0) v.x = value;
                else if (i == 1)
                    v.y = value;
                else
                    v.z = value;
            }
        };
        template<>
        struct VectorTraits<Vector4Int>
        {
            using Scalar = int;
            static constexpr int kDimension = 4;

            static int Get(const Vector4Int &v, int i)
            {
                return i == 0 ? v.x : (i == 1 ? v.y : (i == 2 ? v.z : v.w));
            }

            static void Set(Vector4Int &v, int i, int value)
            {
                if (i == 0) v.x = value;
                else if (i == 1)
                    v.y = value;
                else if (i == 2)
                    v.z = value;
                else
                    v.w = value;
            }
        };


        static const UI::Padding kDefaultLabelMargin = {2.0f, 2.0f, 2.0f, 2.0f};
        template<typename VecT>
        Ref<UIElement> BuildVectorFieldImpl(const String &label, PropertyInfo *property, void *instance, CompositeBuilder::Params *params)
        {
            using Traits = VectorTraits<VecT>;
            using Scalar = typename Traits::Scalar;
            constexpr int N = Traits::kDimension;

            auto hb = MakeRef<UI::HorizontalBox>();
            auto label_text = hb->AddChild<UI::Text>(label);
            label_text->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                    .FillRate(GetLabelFillRate(N)).Margin(kDefaultLabelMargin);

            for (int i = 0; i < N; ++i)
            {
                Scalar value = Traits::Get(property->Get<VecT>(instance), i);

                auto input = hb->AddChild<UI::InputBlock>(std::format("{}", value));
                input->GetSlotAs<UI::LinearSlot>().Margin({2, 2, 2, 4}).SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                        .FillRate(GetInputFillRate(N));

                input->_on_content_changed +=
                        [property, instance, i, params](String content)
                {
                    if constexpr (std::is_same_v<Scalar, float>)
                    {
                        if (auto v = StringUtils::ParseFloat(content))
                        {
                            auto vec = property->Get<VecT>(instance);
                            Traits::Set(vec, i, *v);
                            CompositeBuilder::SetPropertyValue(property, instance, vec, params);
                        }
                    }
                    else
                    {
                        if (auto v = StringUtils::ParseInt32(content))
                        {
                            auto vec = property->Get<VecT>(instance);
                            Traits::Set(vec, i, *v);
                            CompositeBuilder::SetPropertyValue(property, instance, vec, params);
                        }
                    }
                };
            }

            hb->AddPropertyObserver(
                    property->AddObserver(instance,
                                          [hb, property, instance](void *)
                                          {
                                              auto vec = property->Get<VecT>(instance);
                                              for (int i = 0; i < Traits::kDimension; ++i)
                                              {
                                                  auto value = Traits::Get(vec, i);
                                                  hb->ChildAt(i + 1)
                                                          ->As<UI::InputBlock>()
                                                          ->SetContent(
                                                                  [&]()
                                                                  {
                                                                      if constexpr (std::is_floating_point_v<decltype(value)>)
                                                                          return std::format("{:.2f}", value);
                                                                      else
                                                                          return std::format("{}", value);
                                                                  }(),
                                                                  false);
                                              }
                                          }));

            return hb;
        }
        #pragma endregion

        template<typename T>
        Ref<UIElement> BuildScaleFieldImpl(const String &label, PropertyInfo *property, void *instance, CompositeBuilder::Params *params)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).Margin(kDefaultLabelMargin);
            auto data = property->Get<T>(instance);
            String text;
            if constexpr (std::is_floating_point_v<T>)
                text = std::format("{:.2f}", data);
            else
                text = std::to_string(data);
            auto input_block = hb->AddChild<UI::InputBlock>(text);
            input_block->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f}).SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                    .FillRate(GetInputFillRate(3));
            if constexpr (std::is_floating_point_v<T>)
            {
                input_block->_on_content_changed += [property, instance, params](String content)
                {
                    if (auto opt = StringUtils::ParseFloat(content); opt.has_value())
                    {
                        CompositeBuilder::SetPropertyValue(property, instance, static_cast<T>(opt.value()), params);
                    }
                };
            }
            else
            {
                input_block->_on_content_changed += [property, instance, params](String content)
                {
                    if (auto opt = StringUtils::ParseInt32(content); opt.has_value())
                    {
                        CompositeBuilder::SetPropertyValue(property, instance, static_cast<T>(opt.value()), params);
                    }
                };
            }

            hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
            { 
                auto data = property->Get<T>(instance);
                auto input_x = hb->ChildAt(1)->As<UI::InputBlock>();
                String text;
                if constexpr (std::is_floating_point_v<T>)
                    text = std::format("{:.2f}", data);
                else
                    text = std::to_string(data);
                input_x->SetContent(text, false);
            })));
            return hb;
        }
        template<typename T>
        Ref<UIElement> BuildRangeFieldImpl(const String &label, PropertyInfo *property, void *instance, Vector2f range, CompositeBuilder::Params *params)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).Margin(kDefaultLabelMargin);
            auto data = property->Get<T>(instance);
            auto slider = hb->AddChild<UI::Slider>();
            slider->GetSlotAs<UI::LinearSlot>().Margin({10.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            slider->_range = range;
            slider->SetValue(static_cast<f32>(data));
            auto input_block = hb->AddChild<UI::InputBlock>(FormatNumericFieldValue(data));
            input_block->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.2f);
            slider->_on_value_change += [property, instance, params](f32 v)
            {
                CompositeBuilder::SetPropertyValue(property, instance, static_cast<T>(v), params);
            };
            input_block->_on_content_changed += [property, instance, params](String content)
            {
                if (auto value = ParseNumericFieldValue<T>(content); value.has_value())
                {
                    CompositeBuilder::SetPropertyValue(property, instance, value.value(), params);
                }
            };
            hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
            { 
                auto data = property->Get<T>(instance);
                auto slider = hb->ChildAt(1)->As<UI::Slider>();
                auto input = hb->ChildAt(2)->As<UI::InputBlock>();
                slider->SetValue(static_cast<f32>(data), false);
                input->SetContent(FormatNumericFieldValue(data), false);
            })));
            return hb;
        }

        static Ref<UIElement> BuildEnumField(const String &label, PropertyInfo *property, void *instance, CompositeBuilder::Params *params)
        {
            const auto *enum_type = dynamic_cast<const Enum *>(property->GetType());
            if (enum_type == nullptr)
                return nullptr;

            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).Margin(kDefaultLabelMargin);

            u32 current_value = property->Get<u32>(instance);
            const auto &enum_names = enum_type->GetEnumNames();
            Vector<String> items;
            items.reserve(enum_names.size());
            for (const auto *name : enum_names)
                items.emplace_back(*name);

            auto dropdown = hb->AddChild<UI::Dropdown>(items);
            dropdown->GetSlotAs<UI::LinearSlot>().Margin({10.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(GetInputFillRate(3));

            i32 selected_index = enum_type->GetIndexByName(enum_type->GetNameByEnum(current_value));
            if (selected_index < 0)
                selected_index = 0;
            dropdown->SetSelectedIndex(selected_index);

            dropdown->_on_selected_changed += [property, instance, params](i32 idx)
            {
                CompositeBuilder::SetPropertyValue(property, instance, static_cast<u32>(idx), params);
            };

            hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
            {
                auto data = property->Get<u32>(instance);
                const auto *et = dynamic_cast<const Enum *>(property->GetType());
                if (et == nullptr)
                    return;
                i32 idx = et->GetIndexByName(et->GetNameByEnum(data));
                if (idx >= 0)
                    hb->ChildAt(1)->As<UI::Dropdown>()->SetSelectedIndex(idx);
            })));

            return hb;
        }

        #pragma region CompositeBuilder
        void CompositeBuilder::InitBuilders()
        {
            if (s_is_init)
                return;
            s_is_init = true;
            s_builders[StaticClass<Vector2f>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector2f>(label, property, instance, params);
            };
            s_builders[StaticClass<Vector3f>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector3f>(label, property, instance, params);
            };
            s_builders[StaticClass<Vector4f>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector4f>(label, property, instance, params);
            };
            s_builders[StaticClass<Vector2Int>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector2Int>(label, property, instance, params);
            };
            s_builders[StaticClass<Vector3Int>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector3Int>(label, property, instance, params);
            };
            s_builders[StaticClass<Vector4Int>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector4Int>(label, property, instance, params);
            };
            s_builders[StaticClass<f32>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                FloatFieldParams *float_params = dynamic_cast<FloatFieldParams *>(params);
                bool has_range = float_params && (float_params->_range.y > float_params->_range.x);
                if (has_range)
                {
                    return BuildRangeFieldImpl<f32>(label, property, instance, float_params->_range, params);
                }
                else
                {
                    return BuildScaleFieldImpl<f32>(label, property, instance, params);
                }
            };
            s_builders[StaticClass<i32>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                FloatFieldParams *float_params = dynamic_cast<FloatFieldParams *>(params);
                bool has_range = float_params && (float_params->_range.y > float_params->_range.x);
                if (has_range)
                {
                    return BuildRangeFieldImpl<i32>(label, property, instance, float_params->_range, params);
                }
                else
                {
                    return BuildScaleFieldImpl<i32>(label, property, instance, params);
                }
            };
            s_builders[StaticClass<bool>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                auto hb = MakeRef<UI::HorizontalBox>();
                hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).Margin(kDefaultLabelMargin);
                auto data = property->Get<bool>(instance);
                auto checkbox = hb->AddChild<UI::CheckBox>();
                checkbox->GetSlotAs<UI::LinearSlot>().Margin({10.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                        .SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
                checkbox->SetChecked(data);
                checkbox->_on_click += [property, instance, params](bool v)
                {
                    SetPropertyValue(property, instance, v, params);
                };
                hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
                { 
                    auto data = property->Get<bool>(instance);
                    auto checkbox = hb->ChildAt(1)->As<UI::CheckBox>();
                    checkbox->SetChecked(data);
                })));
                return hb;
            };
            s_builders[StaticClass<String>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                auto hb = MakeRef<UI::HorizontalBox>();
                hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).Margin(kDefaultLabelMargin);
                auto data = property->Get<String>(instance);
                auto input_block = hb->AddChild<UI::InputBlock>(data);
                input_block->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f}).SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                        .FillRate(GetInputFillRate(3));
                input_block->_on_content_changed += [property, instance, params](String content)
                {
                    SetPropertyValue(property, instance, content, params);
                };
                hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
                {
                    auto data = property->Get<String>(instance);
                    auto input_x = hb->ChildAt(1)->As<UI::InputBlock>();
                    input_x->SetContent(data, false);
                })));
                return hb;
            };
        }

        Ref<UIElement> CompositeBuilder::BuildPropertyElement(const String &label, PropertyInfo *property, void *instance, Params *params)
        {
            InitBuilders();
            const Type *prop_type = property->GetType();
            auto it = s_builders.find(prop_type);
            if (it != s_builders.end())
            {
                return it->second(label, property, instance, params);
            }
            if (prop_type != nullptr && prop_type->IsEnum())
            {
                return BuildEnumField(label, property, instance, params);
            }
            LOG_WARNING("No UI builder for property type: {}", property->TypeName());
            return nullptr;
        }

        void CompositeBuilder::RegisterBuilder(const Type *type, Builder builder)
        {
            if (s_builders.find(type) != s_builders.end())
            {
                LOG_WARNING("CompositeBuilder: Replacing existing builder for type: {}", type->FullName());
            }
            s_builders[type] = std::move(builder);
        }
        Ref<UIElement> BuildRangeField(const String &label, f32 *data, f32 min, f32 max)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto slider = hb->AddChild<UI::Slider>();
            slider->GetSlotAs<UI::LinearSlot>().Margin({10.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            slider->_range = {min, max};
            slider->SetValue(*data, false);
            auto input_block = hb->AddChild<UI::InputBlock>(FormatNumericFieldValue(*data));
            input_block->GetSlotAs<UI::LinearSlot>().Margin({2.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).FillRate(1.2f);
            slider->_on_value_change += [data, input_block](f32 v)
            {
                *data = v;
                input_block->As<UI::InputBlock>()->SetContent(FormatNumericFieldValue(v), false);
            };
            input_block->As<UI::InputBlock>()->_on_content_changed += [data, slider](String content)
            {
                if (auto value = ParseNumericFieldValue<f32>(content); value.has_value())
                {
                    *data = value.value();
                    slider->As<UI::Slider>()->SetValue(value.value(), false);
                }
            };
            return hb;
        }
        Ref<UIElement> BuildToggleField(const String &label, bool *data)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->GetSlotAs<UI::LinearSlot>().SizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto checkbox = hb->AddChild<UI::CheckBox>();
            checkbox->GetSlotAs<UI::LinearSlot>().Margin({10.0f, 0.0f, 2.0f, 2.0f}).CrossAlignment(UI::EAlignment::kRight)
                    .SizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
            checkbox->_on_click += [data](bool v)
            {
                *data = v;
            };
            return hb;
        }
        #pragma endregion
    }// namespace UI

}// namespace Ailu
