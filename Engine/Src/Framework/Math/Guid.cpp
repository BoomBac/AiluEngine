#include "pch.h"
#include "GlobalMarco.h"
#include "Framework/Math/Guid.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
	Guid Guid::Generate()
	{
		GUID guid;
		Guid al_guid;
		ZeroMemory(&guid, sizeof(guid));
		if (::CoCreateGuid(&guid) == S_OK)
		{
			char bufstring[32 + 1] = { 0 };
			sprintf_s(bufstring, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[7], guid.Data4[7]);
			al_guid._guid = bufstring;
		}
		else
		{
			LOG_ERROR("Generate guid failed!");
			AL_ASSERT(true, "Generate guid failed!");
			al_guid._guid = "error_guid";
		}
		return al_guid;
	}

	Guid::Guid()
	{
	}

	std::string Guid::ToString() const
	{
		return _guid;
	}
	bool Guid::operator==(const Guid& other) const
	{
		return _guid == other._guid;
	}
}
