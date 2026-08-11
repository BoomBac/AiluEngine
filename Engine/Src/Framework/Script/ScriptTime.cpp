#include "Framework/Script/ScriptTime.h"
#include "Framework/Script/ScriptSystem.h"

#include "Framework/Common/TimeMgr.h"

namespace Ailu
{
    f32 ScriptTime::GetDeltaTime() const { return ScriptSystem::Get().GetDeltaTime(); }
    f32 ScriptTime::GetFixedDeltaTime() const { return ScriptSystem::Get().GetFixedDeltaTime(); }
    f32 ScriptTime::GetRenderAlpha() const { return ScriptSystem::Get().GetRenderAlpha(); }
    f32 ScriptTime::GetTime() const { return TimeMgr::TickTimeSinceLoad; }
}
