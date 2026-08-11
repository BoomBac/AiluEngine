#include "Framework/Script/ScriptEngine.h"
#include "Framework/Script/ScriptSystem.h"

#include "Framework/Common/Log.h"
#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
    void ScriptEngine::LogInfo(const String &message) 
    { 
        LOG_INFO("[Lua] {}", message); 
    }
    void ScriptEngine::LogWarning(const String &message) 
    { 
        LOG_WARNING("[Lua] {}", message); 
    }
    void ScriptEngine::LogError(const String &message) 
    { 
        LOG_ERROR("[Lua] {}", message); 
    }
}
