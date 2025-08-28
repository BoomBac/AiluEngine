#include "UIElement.h"
#include "generated/Container.gen.h"

namespace Ailu
{
    namespace UI
    {
        ACLASS()
        class AILU_API Canvas : public UIElement
        {
            GENERATED_BODY()
        public:
            Canvas();
            ~Canvas();
            void Update(f32 dt) final;
            void Render(UIRenderer &r) final;
        };

        ACLASS()
        class AILU_API LinearBox : public UIElement
        {
            GENERATED_BODY()
        public:
            enum class EOrientation
            {
                kHorizontal,
                kVertical
            };
            LinearBox(EOrientation orientation = EOrientation::kHorizontal)
                : _orientation(orientation) 
                {
                _name = std::format("{}_{}", orientation == EOrientation::kHorizontal ? "HorizontalBox" : "VerticalBox",_id);
                }
            void Update(f32 dt) override
            {
                UpdateLayout(dt);
            }
            void Render(UIRenderer &r) override;
        public:
            APROPERTY()
            bool _is_size_to_content = false;//If true, the box will resize to fit its content
        protected:
            void UpdateLayout(f32 dt);
        private:
            EOrientation _orientation;
        };


        ACLASS()
        class AILU_API VerticalBox : public LinearBox
        {
            GENERATED_BODY()
        public:
            VerticalBox() :LinearBox(EOrientation::kVertical) {};
            ~VerticalBox() = default;
        };

        ACLASS()
        class AILU_API HorizontalBox : public LinearBox
        {
            GENERATED_BODY()
        public:
            HorizontalBox() : LinearBox(EOrientation::kHorizontal) {};
            ~HorizontalBox() = default;
        };
    }
}