#ifndef __SYSTEM_INFO_H__
#define __SYSTEM_INFO_H_

struct SystemInfo
{
#if defined(_REVERSED_Z)
    static const bool kReverseZ = true;
#else
    static const bool kReverseZ = false;
#endif
};
#endif //__SYSTEM_INFO_H_