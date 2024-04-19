#ifndef __GLOBAL_MARCO_H__
#define __GLOBAL_MARCO_H__
#include "CompanyEnv.h"
#include "Framework/Common/Log.h"
#include <memory>
#include <vector>
#include <list>
#include <queue>
#include <array>

#ifdef AILU_BUILD_DLL
#define AILU_API __declspec(dllexport)
#else
#define AILU_API __declspec(dllimport)
#endif

#define DEAR_IMGUI
#define _SIMD
#define _PIX_DEBUG

#define CONTACT(x,y) x##y
#define STR(x) #x
#define CONTACT2(x,y) CONTACT(x,y)
#define STR2(x) STR(x)

#ifdef COMPANY_ENV
	#define SOLUTION_DIR C:/AiluEngine/
	#define PROJECT_DIR C:/AiluEngine/Engine/
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

#define TP_ZERO(t) std::get<0>(t)
#define TP_ONE(t) std::get<1>(t)
#define TP_TWO(t) std::get<2>(t)

//#define DECLARE_PRIVATE_PROPERTY(Name,type) \
//public: \
//    void Name(const type& value) { _##Name = value; } \
//    const type& Name() const { return _##Name; } \
//private: \
//    type _##Name;

#define DECLARE_PRIVATE_PROPERTY(member_name,prop_name,type) \
public: \
    void prop_name(const type& value) { _##member_name = value; } \
    const type& prop_name() const{ return _##member_name; } \
private: \
    type _##member_name;

#define DECLARE_PRIVATE_PROPERTY_RO(member_name,prop_name,type) \
public: \
    const type& prop_name() const { return _##member_name; } \
private: \
    type _##member_name;

#define DECLARE_PRIVATE_PROPERTY_PTR(member_name,prop_name,type) \
public: \
    void prop_name(type* value) { _##member_name = value; } \
    type* prop_name() { return _##member_name; } \
private: \
    type* _##member_name;

#define DECLARE_PROTECTED_PROPERTY(member_name,prop_name,type) \
public: \
    void prop_name(const type& value) { _##member_name = value; } \
    const type& prop_name() const { return _##member_name; } \
protected: \
    type _##member_name;

#define DECLARE_PROTECTED_PROPERTY_RO(member_name,prop_name,type) \
public: \
    const type& prop_name() const { return _##member_name; } \
protected: \
    type _##member_name;

#define DECLARE_PROTECTED_PROPERTY_PTR(Name,type) \
public: \
    void Name(type* value) { _##Name = value; } \
    const type* Name() const { return _##Name; } \
protected: \
    type* _##Name;

// Search and remove whitespace from both ends of the string
static std::string TrimEnumString(const std::string& s)
{
    std::string::const_iterator it = s.begin();
    while (it != s.end() && isspace(*it)) { it++; }
    std::string::const_reverse_iterator rit = s.rbegin();
    while (rit.base() != it && isspace(*rit)) { rit++; }
    return std::string(it, rit.base());
}

static void SplitEnumArgs(const char* szArgs, std::string Array[], int nMax)
{
    std::stringstream ss(szArgs);
    std::string strSub;
    int nIdx = 0;
    while (ss.good() && (nIdx < nMax)) {
        getline(ss, strSub, ',');
        Array[nIdx] = TrimEnumString(strSub);
        nIdx++;
    }
};
// This will to define an enum that is wrapped in a namespace of the same name along with ToString(), FromString(), and COUNT
#define DECLARE_ENUM(ename, ...) \
    namespace ename { \
        enum ename { __VA_ARGS__, COUNT }; \
        static std::string _Strings[COUNT]; \
        static const char* ToString(ename e) { \
            if (_Strings[0].empty()) { SplitEnumArgs(#__VA_ARGS__, _Strings, COUNT); } \
            return _Strings[e].c_str(); \
        } \
        static ename FromString(const std::string& strEnum) { \
            if (_Strings[0].empty()) { SplitEnumArgs(#__VA_ARGS__, _Strings, COUNT); } \
            for (int i = 0; i < COUNT; i++) { if (_Strings[i] == strEnum) { return (ename)i; } } \
            return COUNT; \
        } \
    }


//使用dxc编译高版本着色器(>=6.0)，这样的话无法在PIX中看到cbuffer信息
//#define SHADER_DXC

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;

using String = std::string;
using WString = std::wstring;

template<typename T>
using Vector = std::vector<T>;

template<typename T,size_t Size>
using Array = std::array<T,Size>;

template<typename T>
using List = std::list<T>;

template<typename T>
using Queue = std::queue<T>;

template<typename T>
using Scope = std::unique_ptr<T>;

namespace Modules
{
    static String Parser = "[Parser]";
    static String Render = "[Render]";
}

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

