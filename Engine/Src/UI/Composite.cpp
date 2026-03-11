#include "UI/Composite.h"
#include "UI/Basic.h"
#include "UI/Container.h"

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
        Ref<UIElement> BuildVectorFieldImpl(const String &label, PropertyInfo *property, void *instance)
        {
            using Traits = VectorTraits<VecT>;
            using Scalar = typename Traits::Scalar;
            constexpr int N = Traits::kDimension;

            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)
                    ->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                    .SlotFillRate(GetLabelFillRate(N)).SlotMargin(kDefaultLabelMargin);

            for (int i = 0; i < N; ++i)
            {
                Scalar value = Traits::Get(property->Get<VecT>(instance), i);

                auto input = hb->AddChild<UI::InputBlock>(
                                       std::format("{}", value))
                                     ->SlotMargin({2, 0, 2, 2})
                                     .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                     .SlotFillRate(GetInputFillRate(N)).SlotMargin({2, 2, 2, 4})
                                     .As<UI::InputBlock>();

                input->_on_content_changed +=
                        [property, instance, i](String content)
                {
                    if constexpr (std::is_same_v<Scalar, float>)
                    {
                        if (auto v = StringUtils::ParseFloat(content))
                        {
                            auto vec = property->Get<VecT>(instance);
                            Traits::Set(vec, i, *v);
                            property->Set(instance, vec,
                                          PropertyInfo::EPropertyChangeSource::kUI);
                        }
                    }
                    else
                    {
                        if (auto v = StringUtils::ParseInt32(content))
                        {
                            auto vec = property->Get<VecT>(instance);
                            Traits::Set(vec, i, *v);
                            property->Set(instance, vec,
                                          PropertyInfo::EPropertyChangeSource::kUI);
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
                                                  hb->ChildAt(i + 1)
                                                          ->As<UI::InputBlock>()
                                                          ->SetContent(
                                                                  std::format("{:.2f}", Traits::Get(vec, i)), false);
                                              }
                                          }));

            return hb;
        }
        #pragma endregion

        template<typename T>
        Ref<UIElement> BuildScaleFieldImpl(const String &label, PropertyInfo *property, void *instance)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).SlotMargin(kDefaultLabelMargin);
            auto data = property->Get<T>(instance);
            String text;
            if constexpr (std::is_floating_point_v<T>)
                text = std::format("{:.2f}", data);
            else
                text = std::to_string(data);
            auto input_block = hb->AddChild<UI::InputBlock>(text)->SlotMargin({2.0f, 0.0f, 2.0f, 2.0f}).SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).SlotFillRate(GetInputFillRate(3)).As<UI::InputBlock>();
            if constexpr (std::is_floating_point_v<T>)
            {
                input_block->_on_content_changed += [property, instance](String content)
                {
                    if (auto opt = StringUtils::ParseFloat(content); opt.has_value()) 
                    {
                        property->Set<T>(instance,opt.value(),PropertyInfo::EPropertyChangeSource::kUI);
                    }
                };
            }
            else
            {
                input_block->_on_content_changed += [property, instance](String content)
                {
                    if (auto opt = StringUtils::ParseInt32(content); opt.has_value()) 
                    {
                        property->Set<T>(instance,opt.value(),PropertyInfo::EPropertyChangeSource::kUI);
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
        Ref<UIElement> BuildRangeFieldImpl(const String &label, PropertyInfo *property,void* instance,Vector2f range)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).SlotMargin(kDefaultLabelMargin);
            auto data = property->Get<T>(instance);
            auto slider = hb->AddChild<UI::Slider>()
                                ->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f})
                                .SlotAlignmentH(UI::EAlignment::kRight)
                                .SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto)
                                .As<UI::Slider>();
            slider->_range = range;
            slider->SetValue(static_cast<f32>(data));
            slider->_on_value_change += [property, instance](f32 v)
            {
                property->Set<T>(instance,static_cast<T>(v),PropertyInfo::EPropertyChangeSource::kUI);
            };
            hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
            { 
                auto data = property->Get<T>(instance);
                auto slider = hb->ChildAt(1)->As<UI::Slider>();
                slider->SetValue(static_cast<f32>(data), false);
            })));
            return hb;
        }

        #pragma region CompositeBuilder
        void CompositeBuilder::InitBuilders()
        {
            if (s_is_init)
                return;
            s_is_init = true;
            s_builders[StaticClass<Vector2f>()] = [](const String& label, PropertyInfo* property, void* instance, Params* params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector2f>(label, property, instance);
            };
            s_builders[StaticClass<Vector3f>()] = [](const String& label, PropertyInfo* property, void* instance, Params* params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector3f>(label, property, instance);
            };
            s_builders[StaticClass<Vector4f>()] = [](const String& label, PropertyInfo* property, void* instance, Params* params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector4f>(label, property, instance);
            };
            s_builders[StaticClass<Vector2Int>()] = [](const String& label, PropertyInfo* property, void* instance, Params* params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector2Int>(label, property, instance);
            };
            s_builders[StaticClass<Vector3Int>()] = [](const String& label, PropertyInfo* property, void* instance, Params* params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector3Int>(label, property, instance);
            };
            s_builders[StaticClass<Vector4Int>()] = [](const String& label, PropertyInfo* property, void* instance, Params* params) -> Ref<UIElement>
            {
                return BuildVectorFieldImpl<Vector4Int>(label, property, instance);
            };
            s_builders[StaticClass<f32>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                FloatFieldParams* float_params = dynamic_cast<FloatFieldParams*>(params);
                bool has_range = float_params && (float_params->_range.y > float_params->_range.x);
                if (has_range)
                {
                    return BuildRangeFieldImpl<f32>(label, property, instance, float_params->_range);
                }
                else
                {
                    return BuildScaleFieldImpl<f32>(label, property, instance);
                }
            };
            s_builders[StaticClass<i32>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                FloatFieldParams* float_params = dynamic_cast<FloatFieldParams*>(params);
                bool has_range = float_params && (float_params->_range.y > float_params->_range.x);
                if (has_range)
                {
                    return BuildRangeFieldImpl<i32>(label, property, instance, float_params->_range);
                }
                else
                {
                    return BuildScaleFieldImpl<i32>(label, property, instance);
                }
            };
            s_builders[StaticClass<bool>()] = [](const String &label, PropertyInfo *property, void *instance, Params *params) -> Ref<UIElement>
            {
                auto hb = MakeRef<UI::HorizontalBox>();
                hb->AddChild<UI::Text>(label)->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto).SlotMargin(kDefaultLabelMargin);
                auto data = property->Get<bool>(instance);
                auto checkbox = hb->AddChild<UI::CheckBox>()
                                    ->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f})
                                    .SlotAlignmentH(UI::EAlignment::kRight)
                                    .SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto)
                                    .As<UI::CheckBox>();
                checkbox->SetChecked(data);
                checkbox->_on_click += [property, instance](bool v)
                {
                    property->Set<bool>(instance,v,PropertyInfo::EPropertyChangeSource::kUI);
                };
                hb->AddPropertyObserver(std::move(property->AddObserver(instance, [hb, property, instance](void *)
                { 
                    auto data = property->Get<bool>(instance);
                    auto checkbox = hb->ChildAt(1)->As<UI::CheckBox>();
                    checkbox->SetChecked(data);
                })));
                return hb;
            };
        }

        Ref<UIElement> CompositeBuilder::BuildPropertyElement(const String &label, PropertyInfo *property, void *instance, Params *params)
        {
            InitBuilders();
            auto it = s_builders.find(property->GetType());
            if (it != s_builders.end())
            {
                return it->second(label, property, instance, params);
            }
            LOG_WARNING("No UI builder for property type: {}", property->TypeName());
            return nullptr;
        }
        Ref<UIElement> BuildRangeField(const String &label, f32 *data, f32 min, f32 max)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto slider = hb->AddChild<UI::Slider>();
            slider->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            slider->_range = {min, max};
            slider->_on_value_change += [data](f32 v)
            {
                *data = v;
            };
            return hb;
        }
        Ref<UIElement> BuildToggleField(const String &label, bool *data)
        {
            auto hb = MakeRef<UI::HorizontalBox>();
            hb->AddChild<UI::Text>(label)->SlotSizePolicy(UI::ESizePolicy::kFill, UI::ESizePolicy::kAuto);
            auto checkbox = hb->AddChild<UI::CheckBox>();
            checkbox->SlotMargin({10.0f, 0.0f, 2.0f, 2.0f}).SlotAlignmentH(UI::EAlignment::kRight).SlotSizePolicy(UI::ESizePolicy::kAuto, UI::ESizePolicy::kAuto);
            checkbox->_on_click += [data](bool v)
            {
                *data = v;
            };
            return hb;
        }
        #pragma endregion
    }// namespace UI

}// namespace Ailu
