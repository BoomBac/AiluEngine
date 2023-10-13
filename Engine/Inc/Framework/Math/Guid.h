#pragma once
#ifndef __GUID_H__
#define __GUID_H__
#include <string>

using std::string;
namespace Ailu
{
	class Guid
	{
    public:
        static Guid Generate();
        Guid();
        std::string ToString() const;
        bool operator ==(const Guid& other) const;
    private:
        string _guid;
	};
}


#endif // !GUID_H__

