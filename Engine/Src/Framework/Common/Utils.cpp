#include "pch.h"

#include "Framework/Common/Utils.h"

namespace Ailu
{
    static HashMap<std::thread::id, String> s_thread_names;
    static std::mutex s_thread_name_mutex;

    void SetThreadName(const String& name)
    {
        std::lock_guard<std::mutex> lock(s_thread_name_mutex);
#if defined(PLATFORM_WINDOWS)
        SetThreadDescription(GetCurrentThread(), ToWStr(name).c_str());
#endif
        s_thread_names[std::this_thread::get_id()] = name;
    }
    const String& GetThreadName()
    {
        return GetThreadName(std::this_thread::get_id());
    }
    const String& GetThreadName(std::thread::id id)
    {
        auto it = s_thread_names.find(id);
        if (it == s_thread_names.end())
        {
            s_thread_names[id] = std::format("{}", *(u32*)&id);
        }
        return s_thread_names[id];
    }
    const HashMap<std::thread::id, String>& GetAllThreadNameMap()
    {
        return s_thread_names;
    }

}