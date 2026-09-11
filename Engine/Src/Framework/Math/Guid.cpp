#include "pch.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Common/Assert.h"
#include "Framework/Math/Guid.h"
#include "Framework/Common/Log.h"

#include <utility>

namespace Ailu
{
	const Guid Guid::kEmptyGuid = Guid("");
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
            AL_ASSERT_MSG(false, "Generate guid failed!");
            al_guid._guid.clear();
        }
        return al_guid;
    }


	Guid::Guid()
	{
	}

	Guid::Guid(std::string guid) : _guid(std::move(guid))
	{
		const size_t start = _guid.find_first_not_of(" \t\r\n");
		if (start == String::npos)
		{
			_guid.clear();
			return;
		}

		const size_t end = _guid.find_last_not_of(" \t\r\n");
		if (_guid.compare(start, end - start + 1, "null") == 0)
			_guid.clear();
	}

	const std::string& Guid::ToString() const
	{
		return _guid;
	}
	bool Guid::operator==(const Guid& other) const
	{
		return _guid == other._guid;
	}
    bool Guid::operator==(const String &other) const
    {
        return _guid == other;
    }
    bool Guid::IsEmpty() const
    {
        return _guid.empty();
    }
    bool Guid::IsValid() const
    {
        return !_guid.empty();
    }
    size_t GuidHasher::operator()(const Guid &guid) const noexcept
    {
        return std::hash<String>{}(guid.ToString());
    }
}
