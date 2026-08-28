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
        auto *structured_archive = dynamic_cast<FStructedArchive *>(&ar);
        while (class_type != nullptr)
        {
            for (const PropertyInfo &property: class_type->GetProperties())
            {
                // Object identity is runtime metadata.  It was not written by older UI/resource
                // documents, so retain constructor defaults when those legacy fields are absent.
                if (class_type == Object::StaticType() && structured_archive != nullptr &&
                    !structured_archive->HasField(property.Name()))
                    continue;
                property.Deserialize(this, ar);
            }
            class_type = class_type->BaseType();
        }
    }
}
