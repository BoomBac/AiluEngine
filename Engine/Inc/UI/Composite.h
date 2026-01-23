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
            };
        public:
            static Ref<UIElement> BuildPropertyElement(const String &label, PropertyInfo *property, void* instance,Params* params = nullptr);
        private:
            static void InitBuilders();
        private:
            inline static HashMap<const Type*, std::function<Ref<UIElement>(const String &, PropertyInfo*, void*,Params*)>> s_builders;
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