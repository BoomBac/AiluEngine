#include "Framework/Script/ScriptInput.h"
#include "Framework/Script/ScriptSystem.h"

#include "Assets/Asset.h"
#include "Framework/Common/Application.h"
#include "Framework/Common/Log.h"
#include "Framework/Common/ResourceMgr.h"
#include "Input/InputActionAsset.h"
#include "Input/InputAction.h"

namespace Ailu
{
    bool ScriptInput::Pressed(const String &action_name) const
    {
        return IsPressed(action_name);
    }
    bool ScriptInput::Released(const String &action_name) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr) return false;
        const auto *action = application->GetInputSystem().FindAction(action_name);
        return action != nullptr && action->GetPreviousValue().IsActuated() && !action->GetValue().IsActuated();
    }
    bool ScriptInput::Down(const String &action_name) const
    {
        return IsDown(action_name);
    }
    f32 ScriptInput::Axis(const String &action_name) const
    {
        return GetFloat(action_name);
    }
    Vector2f ScriptInput::Axis2(const String &action_name) const
    {
        return GetVector2(action_name);
    }

    bool ScriptInput::IsPressed(const String &action_name) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr) return false;
        const auto *action = application->GetInputSystem().FindAction(action_name);
        return action != nullptr && action->WasPerformedThisFrame();
    }
    bool ScriptInput::IsDown(const String &action_name) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr) return false;
        const auto *action = application->GetInputSystem().FindAction(action_name);
        return action != nullptr && action->GetValue().AsButton();
    }
    f32 ScriptInput::GetFloat(const String &action_name) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr) return 0.0f;
        const auto *action = application->GetInputSystem().FindAction(action_name);
        return action != nullptr ? action->GetValue().AsAxis1D() : 0.0f;
    }
    Vector2f ScriptInput::GetVector2(const String &action_name) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr) return Vector2f::kZero;
        const auto *action = application->GetInputSystem().FindAction(action_name);
        return action != nullptr ? action->GetValue().AsAxis2D() : Vector2f::kZero;
    }
    bool ScriptInput::LoadActionAsset(const String &asset_path) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr || asset_path.empty()) return false;
        Ref<InputActionAsset> input_asset = ResourceMgr::Get().Load<InputActionAsset>(ToWChar(asset_path));
        if (!input_asset)
        {
            LOG_ERROR("Lua input: failed to load InputActionAsset '{}'", asset_path);
            return false;
        }
        auto &input_system = application->GetInputSystem();
        input_system.RemoveAllContexts();
        input_system.LoadAsset(*input_asset);
        return true;
    }
    bool ScriptInput::PushContext(const String &context_name) const
    {
        auto *application = Application::Instance();
        if (application == nullptr || application->GetInputSystemPtr() == nullptr) return false;
        auto &input_system = application->GetInputSystem();
        if (input_system.FindContext(context_name) == nullptr) return false;
        input_system.PushContext(context_name);
        return true;
    }
    bool ScriptInput::Off(u64 subscription_id) const
    {
#if AILU_ENABLE_LUA_SCRIPTING
        return ScriptSystem::Get().Unsubscribe(subscription_id);
#else
        (void) subscription_id;
        return false;
#endif
    }
}
