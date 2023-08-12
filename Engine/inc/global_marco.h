#ifndef __GLOBAL_MARCO_H__

#define AILU_ENGINE

#ifdef AILU_ENGINE
#define AILU_API __declspec(dllexport)
#else
#define AILU_API __declspec(dllimport)
#endif

#endif // !__GLOBAL_MARCO_H__

