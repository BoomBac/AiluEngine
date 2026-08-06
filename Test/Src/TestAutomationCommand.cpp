// Unit tests for the automation command system: manager lifecycle, event
// notification, target validation and the adapter write path.

#include "Automation/AutomationAdapter.h"
#include "Automation/AutomationCommand.h"
#include "Automation/AutomationReadModel.h"
#include "Automation/AutomationService.h"

#include "Scene/Scene.h"

using namespace Ailu;

namespace Ailu::Editor::AutomationCommandTests
{
    namespace
    {
        class TestRecorderCommand final : public IEditorCommand
        {
        public:
            StringView Name() const override { return "TestRecorder"; }
            EditorCommandResult Validate(EditorCommandContext &) override { ++_validated; return {true, "", ""}; }
            EditorCommandResult Execute(EditorCommandContext &) override { ++_executed; return {true, "", ""}; }
            EditorCommandResult Undo(EditorCommandContext &) override { ++_undone; return {true, "", ""}; }
            bool GetPropertyChange(AutomationPropertyChangedEvent &out) const override
            {
                out._property_path = "test.prop";
                out._old_value = AutomationValue(1);
                out._new_value = AutomationValue(2);
                return true;
            }
            int _validated = 0;
            int _executed = 0;
            int _undone = 0;
        };
    }// namespace

    bool TestCommandManagerLifecycle()
    {
        SceneManagement::SceneMgr::Init();
        EditorCommandManager manager;

        auto command = std::make_unique<TestRecorderCommand>();
        auto *raw = command.get();
        if (!manager.ExecuteCommand(std::move(command))._success)
            return false;
        if (raw->_validated != 1 || raw->_executed != 1)
            return false;
        if (!manager.CanUndo() || manager.UndoCount() != 1u)
            return false;

        if (!manager.Undo()._success || raw->_undone != 1)
            return false;
        if (!manager.CanRedo() || manager.RedoCount() != 1u)
            return false;

        if (!manager.Redo()._success || raw->_executed != 2)
            return false;
        if (manager.UndoCount() != 1u)
            return false;

        // A new command clears the redo stack.
        if (!manager.ExecuteCommand(std::make_unique<TestRecorderCommand>())._success)
            return false;
        if (manager.CanRedo())
            return false;

        if (!manager.Undo()._success || !manager.Undo()._success)
            return false;
        if (manager.Undo()._success) // empty undo stack must fail
            return false;
        return true;
    }

    bool TestCommandEventNotification()
    {
        SceneManagement::SceneMgr::Init();
        EditorCommandManager manager;

        int event_count = 0;
        AutomationPropertyChangedEvent last_event;
        manager.OnPropertyChanged().GetEventView() += [&](const AutomationPropertyChangedEvent &event)
        {
            ++event_count;
            last_event = event;
        };

        if (!manager.ExecuteCommand(std::make_unique<TestRecorderCommand>())._success)
            return false;
        if (event_count != 1)
            return false;
        if (last_event._property_path != "test.prop")
            return false;
        if (last_event._old_value.AsInt() != 1 || last_event._new_value.AsInt() != 2)
            return false;
        return true;
    }

    bool TestSetPropertyCommandValidation()
    {
        SceneManagement::SceneMgr::Init();
        EditorCommandManager manager;

        AutomationTargetRef target;
        target._kind = EAutomationTargetKind::kComponent;
        target._entity_guid = Guid::Generate();
        target._component_type = "Ailu.ECS.TransformComponent";
        AutomationValue new_value = AutomationValue(AutomationArray{AutomationValue(1.0), AutomationValue(2.0), AutomationValue(3.0)});

        EditorCommandResult result = manager.ExecuteCommand(std::make_unique<SetPropertyCommand>(target, "_local_transform._position", std::move(new_value)));
        if (result._success)
            return false;
        if (result._error_code != AutomationErrors::kSceneNotOpen)
            return false;
        return true;
    }

    bool TestAdapterWriteComponent()
    {
        RegisterDefaultComponentDescriptors();
        AutomationAdapterRegistry::Get().Initialize();

        const IAutomationTypeAdapter *adapter = AutomationAdapterRegistry::Get().Resolve("Ailu.ECS.TransformComponent");
        if (adapter == nullptr)
            return false;

        ECS::TransformComponent transform;
        transform._local_transform._position = Vector3f(1.0f, 2.0f, 3.0f);
        AutomationValue new_position = AutomationValue(AutomationArray{AutomationValue(7.0), AutomationValue(8.0), AutomationValue(9.0)});
        AutomationResult write = adapter->WriteProperty("Ailu.ECS.TransformComponent", &transform, "_local_transform._position", new_position);
        if (!write._success)
            return false;
        if (transform.GetLocalPosition() != Vector3f(7.0f, 8.0f, 9.0f))
            return false;

        // Read-only field (PersistentId guid) rejects writes.
        const IAutomationTypeAdapter *pid_adapter = AutomationAdapterRegistry::Get().Resolve("Ailu.ECS.PersistentIdComponent");
        if (pid_adapter == nullptr)
            return false;
        ECS::PersistentIdComponent pid;
        AutomationResult rejected = pid_adapter->WriteProperty("Ailu.ECS.PersistentIdComponent", &pid, "_guid", AutomationValue("new-guid"));
        if (rejected._success || rejected._error._code != AutomationErrors::kPropertyNotEditable)
            return false;
        return true;
    }

    bool TestValueConversions()
    {
        AutomationValue vector = AutomationValue(AutomationArray{AutomationValue(1.0), AutomationValue(2.0), AutomationValue(3.0)});
        if (ToVector3f(vector) != Vector3f(1.0f, 2.0f, 3.0f))
            return false;

        const Color color_value(0.25f, 0.5f, 0.75f, 1.0f);
        const Color roundtrip = ToColor(ToAutomationValue(color_value));
        if (roundtrip.r != 0.25f || roundtrip.g != 0.5f || roundtrip.b != 0.75f)
            return false;

        if (ToFloat(AutomationValue(2.5f)) != 2.5f)
            return false;
        if (ToBool(AutomationValue(true)) != true)
            return false;
        if (ToInt(AutomationValue(42)) != 42)
            return false;
        return true;
    }

    bool TestPermissionGate()
    {
        EditorAutomationService service;
        service.Initialize();

        bool invoked = false;
        AutomationMethodDesc desc;
        desc._name = "test.safe_write";
        desc._permission = EAutomationPermission::kSafeWrite;
        service.Registry().Register("test.safe_write", std::move(desc),
                                    [&invoked](EditorAutomationContext &, const AutomationObject &) -> AutomationResult
                                    {
                                        invoked = true;
                                        return AutomationResult::Ok(AutomationValue{});
                                    });

        // Without allow_write the handler must not run.
        AutomationRequest denied_request;
        denied_request._method = "test.safe_write";
        denied_request._context._allow_write = false;
        std::future<AutomationResult> denied_future = service.Submit(std::move(denied_request));
        service.Tick();
        denied_future.wait();
        const AutomationResult denied = denied_future.get();
        if (denied._success || denied._error._code != AutomationErrors::kPermissionDenied)
            return false;
        if (invoked)
            return false;

        // With allow_write the handler runs.
        AutomationRequest allowed_request;
        allowed_request._method = "test.safe_write";
        allowed_request._context._allow_write = true;
        std::future<AutomationResult> allowed_future = service.Submit(std::move(allowed_request));
        service.Tick();
        allowed_future.wait();
        const AutomationResult allowed = allowed_future.get();
        if (!allowed._success || !invoked)
            return false;
        return true;
    }
}// namespace Ailu::Editor::AutomationCommandTests
