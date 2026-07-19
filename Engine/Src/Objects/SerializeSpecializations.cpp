#include "Objects/Serialize.h"
#include "Objects/SerializeSpecializations.h"
#include "Framework/Common/Utils.h"

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


    void SerializerWrapper<WString>::Serialize(void *data, FArchive &ar, const String *name)
    {
        DATA_CHECK_S(WString)
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        String value = ToChar(*static_cast<WString *>(data));
        ar << value;
        if (sar && name)
            sar->EndObject();
    }

    void SerializerWrapper<WString>::Deserialize(void *data, FArchive &ar, const String *name)
    {
        DATA_CHECK_DS(WString)
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        String value;
        ar >> value;
        *static_cast<WString *>(data) = ToWChar(value);
        if (sar && name)
            sar->EndObject();
    }

    void SerializerWrapper<Guid>::Serialize(void *data, FArchive &ar, const String *name)
    {
        DATA_CHECK_S(Guid)
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        String value = static_cast<Guid *>(data)->ToString();
        ar << value;
        if (sar && name)
            sar->EndObject();
    }

    void SerializerWrapper<Guid>::Deserialize(void *data, FArchive &ar, const String *name)
    {
        DATA_CHECK_DS(Guid)
        FStructedArchive *sar = dynamic_cast<FStructedArchive *>(&ar);
        if (sar && name)
            sar->BeginObject(*name);
        String value;
        ar >> value;
        *static_cast<Guid *>(data) = Guid(value);
        if (sar && name)
            sar->EndObject();
    }
    EXPORT_TEMPLATE_SPECIALIZATION(String)
    EXPORT_TEMPLATE_SPECIALIZATION(WString)
    EXPORT_TEMPLATE_SPECIALIZATION(Guid)
}
