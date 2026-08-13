#ifndef UI_COMPOSITE_H
#define UI_COMPOSITE_H
#include "UIElement.h"
#include "Objects/Type.h"

namespace Ailu
{
    namespace UI
    {
        class AILU_API CompositeBuilder
        {
        public:
            struct Params
            {
                virtual ~Params() = default;
                std::function<void(PropertyInfo *)> _on_value_changing;
                std::function<void(PropertyInfo *)> _on_value_changed;
            };
            using Builder = std::function<Ref<UIElement>(const String &, PropertyInfo *, void *, Params *)>;

            static Ref<UIElement> BuildPropertyElement(const String &label, PropertyInfo *property, void *instance, Params *params = nullptr);
            static void RegisterBuilder(const Type *type, Builder builder);

            template<typename TValue>
            static void RegisterBuilder(Builder builder)
            {
                RegisterBuilder(StaticClass<TValue>(), std::move(builder));
            }

            template<typename TValue>
            static void SetPropertyValue(PropertyInfo *property, void *instance, const TValue &value, Params *params)
            {
                if (params != nullptr && params->_on_value_changing)
                    params->_on_value_changing(property);
                property->Set<TValue>(instance, value, PropertyInfo::EPropertyChangeSource::kUI);
                if (params != nullptr && params->_on_value_changed)
                    params->_on_value_changed(property);
            }

        private:
            static void InitBuilders();

        private:
            inline static HashMap<const Type *, Builder> s_builders;
            inline static bool s_is_init = false;
        };
        
        struct FloatFieldParams : public CompositeBuilder::Params
        {
        public:
            Vector2f _range = {0.0f,-1.0f};
            f32 _step = 0.01f;
        };

    }
}
#endif // UI_COMPOSITE_H
