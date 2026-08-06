#include "Automation/AutomationService.h"

#include "Automation/AutomationReadModel.h"

namespace Ailu
{
    namespace Editor
    {
        namespace
        {
            const char *PermissionToString(EAutomationPermission permission)
            {
                switch (permission)
                {
                    case EAutomationPermission::kReadOnly: return "read_only";
                    case EAutomationPermission::kSafeWrite: return "safe_write";
                    case EAutomationPermission::kDestructiveWrite: return "destructive_write";
                    case EAutomationPermission::kExternalEffect: return "external_effect";
                }
                return "read_only";
            }
        }// namespace

        void EditorAutomationService::Initialize()
        {
            if (_is_initialized)
                return;

            // Smoke-test method so the request/response loop is verifiable end to end.
            AutomationMethodDesc ping_desc;
            ping_desc._name = "automation.ping";
            ping_desc._description = "Echo the input arguments back as a smoke test.";
            ping_desc._permission = EAutomationPermission::kReadOnly;
            _registry.Register("automation.ping", std::move(ping_desc),
                               [](EditorAutomationContext &, const AutomationObject &arguments) -> AutomationResult
                               {
                                   AutomationObject result = arguments;
                                   result.emplace("pong", AutomationValue(true));
                                   return AutomationResult::Ok(AutomationValue(std::move(result)));
                               });

            // Introspection: feeds MCP tool-schema generation from one source of truth.
            AutomationMethodDesc list_desc;
            list_desc._name = "automation.list_methods";
            list_desc._description = "Return every registered automation method with its input schema.";
            list_desc._permission = EAutomationPermission::kReadOnly;
            _registry.Register("automation.list_methods", std::move(list_desc),
                               [](EditorAutomationContext &context, const AutomationObject &) -> AutomationResult
                               {
                                   AutomationArray methods;
                                   if (context._registry != nullptr)
                                   {
                                       context._registry->ForEachMethod([&methods](const AutomationMethodDesc &method)
                                       {
                                           AutomationObject entry;
                                           entry.emplace("name", AutomationValue(method._name));
                                           entry.emplace("description", AutomationValue(method._description));
                                           entry.emplace("permission", AutomationValue(PermissionToString(method._permission)));
                                           AutomationArray params;
                                           for (const AutomationParamDesc &param : method._input_schema._params)
                                           {
                                               AutomationObject param_entry;
                                               param_entry.emplace("name", AutomationValue(param._name));
                                               param_entry.emplace("description", AutomationValue(param._description));
                                               param_entry.emplace("type", AutomationValue(ValueTypeToString(param._type)));
                                               param_entry.emplace("required", AutomationValue(param._required));
                                               param_entry.emplace("is_array", AutomationValue(param._is_array));
                                               if (!param._enum_values.empty())
                                               {
                                                   AutomationArray values;
                                                   for (const String &value : param._enum_values)
                                                       values.emplace_back(AutomationValue(value));
                                                   param_entry.emplace("enum_values", AutomationValue(std::move(values)));
                                               }
                                               params.emplace_back(AutomationValue(std::move(param_entry)));
                                           }
                                           entry.emplace("input_schema", AutomationValue(std::move(params)));
                                           methods.emplace_back(AutomationValue(std::move(entry)));
                                       });
                                   }
                                   AutomationObject result;
                                   result.emplace("methods", AutomationValue(std::move(methods)));
                                   return AutomationResult::Ok(AutomationValue(std::move(result)));
                               });

            _is_initialized = true;
            _is_shutting_down = false;
        }

        void EditorAutomationService::Finalize()
        {
            _is_shutting_down = true;
            // Fail any request still in flight so no caller waits forever.
            PendingAutomationRequest pending;
            while (_pending_requests.TryPop(pending))
            {
                if (pending._promise)
                {
                    pending._promise->set_value(AutomationResult::Fail(AutomationErrors::kEditorShuttingDown,
                                                                       "editor automation service is shutting down"));
                }
            }
            _is_initialized = false;
        }

        void EditorAutomationService::Tick()
        {
            if (!_is_initialized)
                return;
            DrainRequests();
        }

        std::future<AutomationResult> EditorAutomationService::Submit(AutomationRequest request)
        {
            auto promise = std::make_shared<std::promise<AutomationResult>>();
            std::future<AutomationResult> future = promise->get_future();
            _pending_requests.Push(PendingAutomationRequest{std::move(request), std::move(promise)});
            return future;
        }

        void EditorAutomationService::DrainRequests()
        {
            u32 processed = 0u;
            PendingAutomationRequest pending;
            while (processed < _max_requests_per_tick && _pending_requests.TryPop(pending))
            {
                if (pending._promise)
                    pending._promise->set_value(Execute(pending._request));
                ++processed;
            }
        }

        AutomationResult EditorAutomationService::Execute(const AutomationRequest &request)
        {
            if (!_is_initialized)
            {
                return AutomationResult::Fail(AutomationErrors::kEditorNotReady,
                                              "editor automation service is not initialized");
            }
            if (_is_shutting_down)
            {
                return AutomationResult::Fail(AutomationErrors::kEditorShuttingDown,
                                              "editor automation service is shutting down");
            }

            // Permission gate: the method's declared permission vs the request context.
            if (const AutomationMethodDesc *desc = _registry.Find(request._method))
            {
                switch (desc->_permission)
                {
                    case EAutomationPermission::kReadOnly:
                        break;
                    case EAutomationPermission::kSafeWrite:
                        if (!request._context._allow_write)
                            return AutomationResult::Fail(AutomationErrors::kPermissionDenied,
                                                          "safe write is not allowed for this caller");
                        break;
                    case EAutomationPermission::kDestructiveWrite:
                    case EAutomationPermission::kExternalEffect:
                        if (!request._context._allow_destructive)
                            return AutomationResult::Fail(AutomationErrors::kPermissionDenied,
                                                          "destructive write is not allowed for this caller");
                        break;
                }
            }

            EditorAutomationContext context;
            context._request = &request._context;
            context._registry = &_registry;
            return _registry.Invoke(request._method, context, request._arguments);
        }
    }// namespace Editor
}// namespace Ailu
