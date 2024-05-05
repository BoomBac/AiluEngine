#include "pch.h"
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
	const Guid Guid::kEmptyGuid = Guid("null guid");
    Guid Guid::Generate()
    {
        GUID guid;
        Guid al_guid;
        ZeroMemory(&guid, sizeof(guid));
        if (::CoCreateGuid(&guid) == S_OK)
        {
			char guid_cstr[39];
			snprintf(guid_cstr, sizeof(guid_cstr),
				"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
				guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

            al_guid._guid = guid_cstr;
        }
        else
        {
            LOG_ERROR("Generate guid failed!");
            AL_ASSERT_MSG(true, "Generate guid failed!");
            al_guid._guid = "error_guid";
        }
        return al_guid;
    }


	Guid::Guid()
	{
	}

	Guid::Guid(std::string guid) : _guid(guid)
	{
	}

	const std::string& Guid::ToString() const
	{
		return _guid;
	}
	bool Guid::operator==(const Guid& other) const
	{
		return _guid == other._guid;
	}
}
