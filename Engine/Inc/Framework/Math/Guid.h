#pragma once
#ifndef __GUID_H__
#define __GUID_H__
#include "GlobalMarco.h"
#include <string>

namespace Ailu
{
    class AILU_API Guid
    {
    public:
        static Guid Generate();
        Guid();
        explicit Guid(std::string guid);
        [[nodiscard]] const String & ToString() const;
        bool operator ==(const Guid& other) const;
        bool operator ==(const String& other) const;
        bool operator<(const Guid& other) const { return _guid < other._guid; }
        static const Guid& EmptyGuid() { return kEmptyGuid; }

    private:
        std::string _guid;

        static const Guid kEmptyGuid;
    };
}


#endif // !GUID_H__

