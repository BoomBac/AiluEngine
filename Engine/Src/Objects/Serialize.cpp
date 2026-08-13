#include "Objects/Serialize.h"
#include "Objects/Type.h"

namespace Ailu
{
    void SerializeObject::Serialize(FArchive &ar)
    {
        const Type *class_type = GetType();
        if (FStructedArchive *structured_archive = dynamic_cast<FStructedArchive *>(&ar); structured_archive != nullptr)
        {
            structured_archive->BeginObject("_type_name");
            *structured_archive << class_type->FullName();
            structured_archive->EndObject();
        }

        while (class_type != nullptr)
        {
            for (const PropertyInfo &property: class_type->GetProperties())
                property.Serialize(this, ar);
            class_type = class_type->BaseType();
        }
    }

    void SerializeObject::Deserialize(FArchive &ar)
    {
        const Type *class_type = GetType();
        while (class_type != nullptr)
        {
            for (const PropertyInfo &property: class_type->GetProperties())
                property.Deserialize(this, ar);
            class_type = class_type->BaseType();
        }
    }
}
