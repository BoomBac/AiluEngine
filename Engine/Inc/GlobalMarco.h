#ifndef __GLOBAL_MARCO_H__
#define __GLOBAL_MARCO_H__


#ifdef AILU_BUILD_DLL
#define AILU_API __declspec(dllexport)
#else
#define AILU_API __declspec(dllimport)
#endif
#define CONTACT(x,y) x##y
#define STR(x) #x
#define CONTACT2(x,y) CONTACT(x,y)
#define STR2(x) STR(x)

#define SOLUTION_DIR F:/ProjectCpp/AiluEngine/
#define PROJECT_DIR F:/ProjectCpp/AiluEngine/Engine/
#define GET_ENGINE_FULL_PATH(file) STR2(CONTACT2(PROJECT_DIR,file))

#define CONTACTW(x,y) x##y
#define STRW(x) L#x
#define CONTACTW2(x,y) CONTACT(x,y)
#define STRW2(x) STRW(x)

#define GET_ENGINE_FULL_PATHW(file) STRW2(CONTACTW2(PROJECT_DIR,file))

#endif // !__GLOBAL_MARCO_H__

