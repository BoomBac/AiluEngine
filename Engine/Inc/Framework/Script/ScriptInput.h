#pragma once

#include "Framework/Core/Delegate.h"
#include "Framework/Core/CoreMinimal.h"
#include "Framework/Core/String.h"
#include "Framework/Math/ALMath.hpp"

#include "generated/ScriptInput.gen.h"

namespace Ailu
{
    ASTRUCT()
    struct AILU_API ScriptInput
    {
        GENERATED_BODY()
        AFUNCTION(Script)
        bool IsPressed(const String &action_name) const;
        AFUNCTION(Script)
        bool IsDown(const String &action_name) const;
        AFUNCTION(Script)
        f32 GetFloat(const String &action_name) const;
        AFUNCTION(Script)
        Vector2f GetVector2(const String &action_name) const;
        AFUNCTION(Script)
        bool LoadActionAsset(const String &asset_path) const;
        AFUNCTION(Script)
        bool PushContext(const String &context_name) const;
        AEVENT(Script, KeyIndex = 0, KeyName = action_name)
        DECLARE_DELEGATE(on_performed, String);
        AEVENT(Script, KeyIndex = 0, KeyName = action_name)
        DECLARE_DELEGATE(on_value_changed, String, f32);

        void NotifyPerformed(const String &action_name) { _on_performed_delegate.Invoke(action_name); }
        void NotifyValueChanged(const String &action_name, f32 value) { _on_value_changed_delegate.Invoke(action_name, value); }
    };
}
