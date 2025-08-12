#include "Objects/Serialize.h"
#include "Objects/SerializeSpecializations.h"

namespace Ailu
{
    #define EXPORT_TEMPLATE_SPECIALIZATION(type) template struct AILU_API SerializerWrapper<type>;
    void SerializerWrapper<String>::Serialize(void *data, FArchive &ar, const String *name)
    {
        DATA_CHECK_S(String)
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        ar << *static_cast<String *>(data);
        if (sar && name)
            sar->EndObject();
    }

    void SerializerWrapper<String>::Deserialize(void *data, FArchive &ar, const String *name)
    {
        DATA_CHECK_DS(String)
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        ar >> *static_cast<String *>(data);
        if (sar && name)
            sar->EndObject();
    }

    EXPORT_TEMPLATE_SPECIALIZATION(String)
}