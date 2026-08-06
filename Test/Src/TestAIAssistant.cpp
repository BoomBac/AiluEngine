// Unit tests for the built-in AI assistant loop: read auto-execute, write
// preview, apply / reject, and immediate-finish for read-only plans.

#include "Automation/AIAssistant.h"
#include "Automation/AutomationRegistry.h"

#include "Scene/Scene.h"

using namespace Ailu;

namespace Ailu::Editor::AIAssistantTests
{
    namespace
    {
        class TestProvider final : public IAIProvider
        {
        public:
            Vector<AIPlannedAction> _plan;
            Vector<AIPlannedAction> GeneratePlan(const String &, const String &) override { return _plan; }
            String GenerateResponse(const String &, const Vector<AutomationResult> &) override { return "ok"; }
        };

        EditorAutomationRegistry *MakeRegistry()
        {
            static EditorAutomationRegistry s_registry;
            static bool initialized = false;
            if (!initialized)
            {
                AutomationMethodDesc read_desc;
                read_desc._name = "test.read";
                read_desc._permission = EAutomationPermission::kReadOnly;
                s_registry.Register("test.read", std::move(read_desc),
                                    [](EditorAutomationContext &, const AutomationObject &) -> AutomationResult
                                    { return AutomationResult::Ok(AutomationValue(42)); });
                AutomationMethodDesc write_desc;
                write_desc._name = "test.write";
                write_desc._permission = EAutomationPermission::kSafeWrite;
                s_registry.Register("test.write", std::move(write_desc),
                                    [](EditorAutomationContext &, const AutomationObject &) -> AutomationResult
                                    { return AutomationResult::Ok(AutomationValue(7)); });
                initialized = true;
            }
            return &s_registry;
        }
    }// namespace

    bool TestAILoopReadAndPreview()
    {
        SceneManagement::SceneMgr::Init();
        AIAssistantService &service = AIAssistantService::Get();
        service.Reset();
        service.SetRegistry(MakeRegistry());

        auto provider = std::make_unique<TestProvider>();
        AIPlannedAction read;
        read._operation = "test.read";
        AIPlannedAction write;
        write._operation = "test.write";
        provider->_plan.push_back(read);
        provider->_plan.push_back(write);
        service.SetProvider(std::move(provider));

        if (!service.SubmitRequest("inspect and modify"))
            return false;
        if (!service.HasPendingPreview())
            return false;
        // The read action executed immediately; the write is staged.
        if (service.CurrentTurn()._results.size() != 1u || service.CurrentTurn()._results[0]._data.AsInt() != 42)
            return false;

        AutomationResult apply = service.Apply();
        if (!apply._success || service.HasPendingPreview())
            return false;
        if (service.History().size() != 1u)
            return false;
        const AIConversationTurn &turn = service.History().back();
        if (turn._results.size() != 2u || turn._results[1]._data.AsInt() != 7)
            return false;
        if (!turn._applied || turn._ai_response != "ok")
            return false;
        return true;
    }

    bool TestAIReject()
    {
        SceneManagement::SceneMgr::Init();
        AIAssistantService &service = AIAssistantService::Get();
        service.Reset();
        service.SetRegistry(MakeRegistry());

        auto provider = std::make_unique<TestProvider>();
        AIPlannedAction write;
        write._operation = "test.write";
        provider->_plan.push_back(write);
        service.SetProvider(std::move(provider));

        if (!service.SubmitRequest("modify"))
            return false;
        if (!service.HasPendingPreview())
            return false;
        service.Reject();
        if (service.HasPendingPreview() || service.History().size() != 1u)
            return false;
        const AIConversationTurn &turn = service.History().back();
        if (!turn._rejected || !turn._results.empty())
            return false;
        return true;
    }

    bool TestAIImmediateFinishForReadOnly()
    {
        SceneManagement::SceneMgr::Init();
        AIAssistantService &service = AIAssistantService::Get();
        service.Reset();
        service.SetRegistry(MakeRegistry());

        auto provider = std::make_unique<TestProvider>();
        AIPlannedAction read;
        read._operation = "test.read";
        provider->_plan.push_back(read);
        service.SetProvider(std::move(provider));

        if (!service.SubmitRequest("inspect"))
            return false;
        if (service.HasPendingPreview() || service.History().size() != 1u)
            return false;
        return true;
    }
}// namespace Ailu::Editor::AIAssistantTests
