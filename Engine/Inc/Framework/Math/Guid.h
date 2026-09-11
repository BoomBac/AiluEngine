#pragma once
#ifndef __GUID_H__
#define __GUID_H__
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include <string>

namespace Ailu
{
    class AILU_API Guid
    {
    public:
        static Guid Generate();
        Guid();
        // An empty Guid is represented only by an empty string; legacy sentinels are normalized on construction.
        explicit Guid(std::string guid);
        [[nodiscard]] const String & ToString() const;
        bool operator ==(const Guid& other) const;
        bool operator ==(const String& other) const;
        bool operator<(const Guid& other) const { return _guid < other._guid; }
        static const Guid& EmptyGuid() { return kEmptyGuid; }

        [[nodiscard]] bool IsEmpty() const;
        [[nodiscard]] bool IsValid() const;

    private:
        std::string _guid;

        static const Guid kEmptyGuid;
    };

    struct AILU_API GuidHasher
    {
        size_t operator()(const Guid &guid) const noexcept;
    };
}


#endif // !GUID_H__

