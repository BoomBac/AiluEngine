#pragma once

#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/Containers/Vector.h"

#if AILU_ENABLE_LUA_SCRIPTING
#include <sol/sol.hpp>
#endif

namespace Ailu
{
#if AILU_ENABLE_LUA_SCRIPTING
    using ScriptLuaBindingFunction = void (*)(sol::state &lua);

    class AILU_API ScriptLuaBindingRegister
    {
    public:
        explicit ScriptLuaBindingRegister(ScriptLuaBindingFunction function);

        static void RegisterAll(sol::state &lua);

    private:
        static Vector<ScriptLuaBindingFunction> &GetFunctions();
    };
#endif
}
