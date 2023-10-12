#ifndef __GLOBAL_MARCO_H__
#define __GLOBAL_MARCO_H__
#include "CompanyEnv.h"
#include "Framework/Common/Log.h"
#include <memory>

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

#define CONTACTW(x,y) x##y
#define STRW(x) L#x
#define CONTACTW2(x,y) CONTACT(x,y)
#define STRW2(x) STRW(x)
#define RES_PATH CONTACT2(PROJECT_DIR,Res/)

#define GET_ENGINE_FULL_PATH(file) STR2(CONTACT2(PROJECT_DIR,file))
#define GET_ENGINE_FULL_PATHW(file) STRW2(CONTACTW2(PROJECT_DIR,file))

#define GET_RES_PATH(file) STR2(CONTACT2(RES_PATH,file))
#define GET_RES_PATHW(file) STRW2(CONTACTW2(RES_PATH,file))


#define BIT(x) (1 << x)

#define DESTORY_PTR(ptr) if(ptr != nullptr) {delete ptr;ptr = nullptr;}
#define DESTORY_PTRARR(ptr) if(ptr != nullptr) {delete[] ptr;ptr = nullptr;}

#define INIT_CHECK(obj,class) if(!obj->_b_init) {LOG_ERROR("{} hasn't been init!",#class) return;}

#define HIGH_BIT(x, n) ((x) >> (n))
#define LOW_BIT(x, n) ((x) & ((1 << (n)) - 1))

#define AL_ASSERT(x,msg) if(x) throw(std::runtime_error(msg));

//#define DECLARE_PRIVATE_PROPERTY(Name,type) \
//public: \
//    void Name(const type& value) { _##Name = value; } \
//    const type& Name() const { return _##Name; } \
//private: \
//    type _##Name;

#define DECLARE_PRIVATE_PROPERTY(member_name,prop_name,type) \
public: \
    void prop_name(const type& value) { _##member_name = value; } \
    const type& prop_name() const { return _##member_name; } \
private: \
    type _##member_name;

//#define DECLARE_PROTECTED_PROPERTY(Name,type) \
//public: \
//    void Name(const type& value) { _##Name = value; } \
//    const type& Name() const { return _##Name; } \
//protected: \
//    type _##Name;

#define DECLARE_PROTECTED_PROPERTY(member_name,prop_name,type) \
public: \
    void prop_name(const type& value) { _##member_name = value; } \
    const type& prop_name() const { return _##member_name; } \
protected: \
    type _##member_name;

#define DECLARE_PROTECTED_PROPERTY_PTR(Name,type) \
public: \
    void Name(type* value) { _##Name = value; } \
    const type* Name() const { return _##Name; } \
private: \
    type* _##Name;
	


//使用dxc编译高版本着色器(>=6.0)，这样的话无法在PIX中看到cbuffer信息
//#define SHADER_DXC

using string = std::string;

template<typename T>
using vector = std::vector<T>;

template<typename T>
using Scope = std::unique_ptr<T>;

template<typename T>
Scope<T> MakeScope()
{
	return std::move(std::make_unique<T>());
}

template<typename T, typename... Args>
Scope<T> MakeScope(Args&&... args) 
{
	return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T>
Ref<T> MakeRef() 
{
	return std::make_shared<T>();
}

template<typename T, typename... Args>
Ref<T> MakeRef(Args&&... args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
}

#endif // !__GLOBAL_MARCO_H__

