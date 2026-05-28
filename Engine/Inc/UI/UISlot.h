//
// Created by 22292 on 2024/10/28.
//

#ifndef AILU_UISLOT_H
#define AILU_UISLOT_H

#include "Framework/Math/ALMath.hpp"
#include "Objects/Serialize.h"
#include "generated/UISlot.gen.h"

namespace Ailu
{
    namespace UI
    {
        AENUM()
        enum class EAlignment
        {
            kLeft,
            kCenter,
            kRight,
            kTop,
            kBottom,
            kFill
        };

        AENUM()
        enum class ESlotType
        {
            kCanvas,
            kVerticalBox,
            kHorizontalBox
        };

        //用于vertical/horizontal box布局
        AENUM()
        enum class ESizePolicy
        {
            kFixed,//使用slot.size即使不是canvas
            kFill, // 填充剩余空间,如果有多个fill则平分剩余空间
            kAuto  // 根据内容自适应大小
        };

        ASTRUCT()
        struct Padding
        {
            GENERATED_BODY()
            f32 _l = 0.f;
            f32 _t = 0.f;
            f32 _r = 0.f;
            f32 _b = 0.f;
            Padding() = default;
            Padding(f32 l, f32 t, f32 r, f32 b) : _l(l), _t(t), _r(r), _b(b) {};
            Padding(f32 v) : Padding(v, v, v, v) {};
            Padding(Vector4f v) : Padding(v.x, v.y, v.z, v.w) {};
            void Serialize(FArchive &ar)
            {
                Vector4f v{_l, _t, _r, _b};
                SerializePrimitive<Vector4f>(&v, ar);
            };
            void Deserialize(FArchive &ar)
            {
                Vector4f v;
                DeserializePrimitive<Vector4f>(&v, ar);
                memcpy(this, &v, sizeof(Padding));
            };
            String ToString() const
            {
                return std::format("{},{},{},{}", _l, _t, _r, _b);
            }
        };

        ASTRUCT()
        struct Slot
        {
            GENERATED_BODY()
            Slot(Vector2f size) : _anchor(), _position(), _size(size) {};
            Slot() : Slot({100.f, 100.f}) {};
            APROPERTY()
            Vector2f _size;
            APROPERTY()
            Vector2f _anchor = Vector2f::kZero;
            APROPERTY()
            Vector2f _position = Vector2f::kZero;
            APROPERTY()
            EAlignment _alignment_h = EAlignment::kLeft;
            APROPERTY()
            EAlignment _alignment_v = EAlignment::kCenter;
            APROPERTY()
            Padding _margin;// ltrb，元素外边距
            APROPERTY()
            ESlotType _type = ESlotType::kCanvas;
            APROPERTY()
            ESizePolicy _size_policy_h = ESizePolicy::kFill;//用于linear box slot
            APROPERTY()
            ESizePolicy _size_policy_v = ESizePolicy::kAuto;//用于linear box slot
            APROPERTY()
            bool _is_size_to_content = false;//用于canvas slot
            APROPERTY()
            f32 _fill_rate = 1.0f;//linear box slot,所有fill的fill_rate之和为总权重
        };

        ACLASS()
        class AILU_API UISlot : public SerializeObject
        {
            GENERATED_BODY()
        public:
            virtual ~UISlot() = default;
            APROPERTY()
            Padding _margin;
        };

        ACLASS()
        class AILU_API CanvasSlot : public UISlot
        {
            GENERATED_BODY()
        public:
            APROPERTY()
            Vector2f _anchor = Vector2f::kZero;
            APROPERTY()
            Vector2f _position = Vector2f::kZero;
            APROPERTY()
            Vector2f _size = {100.0f, 100.0f};
            APROPERTY()
            bool _size_to_content = false;
            APROPERTY()
            EAlignment _alignment_h = EAlignment::kLeft;
            APROPERTY()
            EAlignment _alignment_v = EAlignment::kCenter;
        };

        ACLASS()
        class AILU_API LinearSlot : public UISlot
        {
            GENERATED_BODY()
        public:
            APROPERTY()
            ESizePolicy _size_policy_h = ESizePolicy::kFill;
            APROPERTY()
            ESizePolicy _size_policy_v = ESizePolicy::kAuto;
            APROPERTY()
            f32 _fill_rate = 1.0f;
            APROPERTY()
            EAlignment _cross_align = EAlignment::kCenter;
        };
    }// namespace UI
}// namespace Ailu

#endif// AILU_UISLOT_H
