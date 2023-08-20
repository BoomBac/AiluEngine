#ifndef __GLOBAL_MARCO_H__
#define __GLOBAL_MARCO_H__
#include "CompanyEnv.h"
#include "Framework/Common/Log.h"

#ifdef AILU_BUILD_DLL
#define AILU_API __declspec(dllexport)
#else
#define AILU_API __declspec(dllimport)
#endif
#define CONTACT(x,y) x##y
#define STR(x) #x
#define CONTACT2(x,y) CONTACT(x,y)
#define STR2(x) STR(x)

#ifdef COMPANY_ENV
	#define SOLUTION_DIR E:/AiluEngine/
	#define PROJECT_DIR E:/AiluEngine/Engine/
#else
	#define SOLUTION_DIR F:/ProjectCpp/AiluEngine/
	#define PROJECT_DIR F:/ProjectCpp/AiluEngine/Engine/
#endif // COMPANY_ENV

#define GET_ENGINE_FULL_PATH(file) STR2(CONTACT2(PROJECT_DIR,file))

#define CONTACTW(x,y) x##y
#define STRW(x) L#x
#define CONTACTW2(x,y) CONTACT(x,y)
#define STRW2(x) STRW(x)

#define GET_ENGINE_FULL_PATHW(file) STRW2(CONTACTW2(PROJECT_DIR,file))

#define BIT(x) (1 << x)

#define DESTORY_PTR(ptr) if(ptr != nullptr) {delete ptr;ptr = nullptr;}

#define INIT_CHECK(obj,class) if(!obj->_b_init) {LOG_ERROR("{} hasn't been init!",#class) return;}

#define HIGH_BIT(x, n) ((x) >> (n))
#define LOW_BIT(x, n) ((x) & ((1 << (n)) - 1))

#endif // !__GLOBAL_MARCO_H__

