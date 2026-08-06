#include "Automation/AIAssistant.h"

#include "Common/Selection.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"

#include <format>

namespace Ailu
{
    namespace Editor
    {
        // -----------------------------------------------------------------------
        // DemoAIProvider
        // -----------------------------------------------------------------------
        namespace
        {
            constexpr const char *kLightColorPath = "_light._light_color";
        }

        Vector<AIPlannedAction> DemoAIProvider::GeneratePlan(const String &user_request, const String &selection_context)
        {
            Vector<AIPlannedAction> plan;
            const String lowered = user_request;

            // Extract a probable target name from the request.
            String target_name;
            {
                // Very light heuristic: the first quoted token, else the selection's first entity.
                const size_t quote = user_request.find('"');
                if (quote != String::npos)
                {
                    const size_t end = user_request.find('"', quote + 1);
                    if (end != String::npos)
                        target_name = user_request.substr(quote + 1, end - quote - 1);
                }
            }

            // Read step: query the scene so the request is grounded.
            AIPlannedAction query;
            query._operation = "scene.query_entities";
            if (!target_name.empty())
                query._arguments.emplace("name", AutomationValue(target_name));
            else
                query._arguments.emplace("limit", AutomationValue(i64(5)));
            query._summary = target_name.empty() ? "Query scene entities" : std::format("Query entities named '{}'", target_name);
            plan.emplace_back(std::move(query));

            // Write/destructive steps based on simple keyword patterns.
            if (lowered.find("delete") != String::npos || lowered.find("remove") != String::npos ||
                lowered.find("删除") != String::npos)
            {
                AIPlannedAction delete_action;
                delete_action._operation = "scene.delete_entity";
                delete_action._arguments.emplace("entity_guid", AutomationValue("$selected"));
                delete_action._summary = "Delete the target entity";
                plan.emplace_back(std::move(delete_action));
            }
            else if (lowered.find("color") != String::npos || lowered.find("颜色") != String::npos)
            {
                AIPlannedAction write;
                write._operation = "scene.set_property";
                AutomationObject target;
                target.emplace("kind", AutomationValue("component"));
                target.emplace("entity_guid", AutomationValue("$selected"));
                target.emplace("component_type", AutomationValue("Ailu.ECS.LightComponent"));
                AutomationObject value;
                value.emplace("type", AutomationValue("color"));
                value.emplace("space", AutomationValue("srgb"));
                AutomationArray rgba;
                rgba.emplace_back(AutomationValue(1.0));
                rgba.emplace_back(AutomationValue(0.5));
                rgba.emplace_back(AutomationValue(0.25));
                rgba.emplace_back(AutomationValue(1.0));
                value.emplace("value", AutomationValue(std::move(rgba)));
                write._arguments.emplace("target", AutomationValue(std::move(target)));
                write._arguments.emplace("property_path", AutomationValue(kLightColorPath));
                write._arguments.emplace("value", AutomationValue(std::move(value)));
                write._summary = "Set the light color to warm orange";
                plan.emplace_back(std::move(write));
            }
            (void) selection_context;
            return plan;
        }

        String DemoAIProvider::GenerateResponse(const String &user_request, const Vector<AutomationResult> &results)
        {
            u32 failed = 0u;
            for (const AutomationResult &result : results)
            {
                if (!result._success)
                    ++failed;
            }
            if (failed == 0u)
                return std::format("Done. {} step(s) completed successfully for: {}", results.size(), user_request);
            return std::format("{} step(s) failed out of {} for: {}", failed, results.size(), user_request);
        }

        // -----------------------------------------------------------------------
        // AIAssistantService
        // -----------------------------------------------------------------------
        AIAssistantService &AIAssistantService::Get()
        {
            static AIAssistantService s_instance;
            return s_instance;
        }

        void AIAssistantService::SetRegistry(EditorAutomationRegistry *registry)
        {
            _registry = registry;
        }

        void AIAssistantService::SetProvider(Scope<IAIProvider> provider)
        {
            _provider = std::move(provider);
        }

        String AIAssistantService::BuildSelectionContext()
        {
            SceneManagement::Scene *scene = SceneManagement::SceneMgr::Get().ActiveScene();
            if (scene == nullptr)
                return String{};
            String context;
            bool first = true;
            for (const ECS::Entity entity : Selection::SelectedEntities())
            {
                const Guid *guid = scene->FindEntityGuid(entity);
                const ECS::TagComponent *tag = scene->GetRegister().GetComponent<ECS::TagComponent>(entity);
                if (!first)
                    context += ", ";
                first = false;
                context += tag ? tag->_name : String{"<unnamed>"};
                context += "(";
                context += guid ? guid->ToString() : String{"?"};
                context += ")";
            }
            return context.empty() ? "no selection" : context;
        }

        bool AIAssistantService::SubmitRequest(String user_message)
        {
            if (_has_pending)
                return false;
            if (_provider == nullptr || _registry == nullptr)
                return false;

            _current = AIConversationTurn{};
            _current._user_message = std::move(user_message);
            _current._selection_context = BuildSelectionContext();
            _staged_indices.clear();

            _current._planned_actions = _provider->GeneratePlan(_current._user_message, _current._selection_context);

            for (size_t i = 0; i < _current._planned_actions.size(); ++i)
            {
                const AIPlannedAction &action = _current._planned_actions[i];
                const AutomationMethodDesc *desc = _registry->Find(action._operation);
                const EAutomationPermission permission = desc != nullptr ? desc->_permission : EAutomationPermission::kReadOnly;

                if (permission == EAutomationPermission::kReadOnly)
                {
                    // Read actions run immediately.
                    _current._results.emplace_back(ExecuteAction(action));
                }
                else
                {
                    _staged_indices.push_back(i);
                    _current._had_preview = true;
                }
            }

            if (_staged_indices.empty())
            {
                FinishTurn();
                return true;
            }
            _has_pending = true;
            return true;
        }

        namespace
        {
            // Substitute "$selected" in string values with the first selected entity's guid.
            String FirstSelectedEntityGuid()
            {
                SceneManagement::Scene *scene = SceneManagement::SceneMgr::Get().ActiveScene();
                if (scene == nullptr)
                    return String{};
                for (const ECS::Entity entity : Selection::SelectedEntities())
                {
                    const Guid *guid = scene->FindEntityGuid(entity);
                    if (guid != nullptr)
                        return guid->ToString();
                }
                return String{};
            }

            AutomationValue ResolveSelectionRef(const AutomationValue &value, const String &selected_guid)
            {
                if (value.IsString())
                {
                    const String &text = value.AsString();
                    if (text == "$selected")
                        return selected_guid.empty() ? value : AutomationValue(selected_guid);
                    return value;
                }
                if (value.IsArray())
                {
                    AutomationArray array;
                    for (const AutomationValue &item : value.AsArray())
                        array.emplace_back(ResolveSelectionRef(item, selected_guid));
                    return AutomationValue(std::move(array));
                }
                if (value.IsObject())
                {
                    AutomationObject object;
                    for (const auto &[key, item] : value.AsObject())
                        object.emplace(key, ResolveSelectionRef(item, selected_guid));
                    return AutomationValue(std::move(object));
                }
                return value;
            }
        }// namespace

        AutomationResult AIAssistantService::ExecuteAction(const AIPlannedAction &action)
        {
            if (_registry == nullptr)
                return AutomationResult::Fail(AutomationErrors::kInternalError, "automation registry is not set");
            AutomationObject resolved_arguments;
            const String selected_guid = FirstSelectedEntityGuid();
            for (const auto &[key, value] : action._arguments)
                resolved_arguments.emplace(key, ResolveSelectionRef(value, selected_guid));

            AutomationRequestContext request_context;
            request_context._caller = "ai";
            request_context._allow_write = true;
            request_context._allow_destructive = true;
            request_context._interactive = true;
            EditorAutomationContext context;
            context._request = &request_context;
            context._registry = _registry;
            return _registry->Invoke(action._operation, context, resolved_arguments);
        }

        AutomationResult AIAssistantService::Apply()
        {
            if (!_has_pending)
                return AutomationResult::Fail(AutomationErrors::kCommandFailed, "no pending preview to apply");
            AutomationResult first_failure;
            bool has_failure = false;
            for (const size_t index : _staged_indices)
            {
                AutomationResult result = ExecuteAction(_current._planned_actions[index]);
                _current._results.emplace_back(result);
                if (!result._success && !has_failure)
                {
                    first_failure = result;
                    has_failure = true;
                }
            }
            _current._applied = true;
            FinishTurn();
            return has_failure ? first_failure : AutomationResult::Ok(AutomationValue{});
        }

        void AIAssistantService::Reject()
        {
            if (!_has_pending)
                return;
            _current._rejected = true;
            _current._ai_response = "The proposed changes were rejected; nothing was modified.";
            FinishTurn();
        }

        void AIAssistantService::Cancel()
        {
            if (!_has_pending)
                return;
            _current._cancelled = true;
            _current._ai_response = "The request was cancelled; nothing was modified.";
            FinishTurn();
        }

        void AIAssistantService::FinishTurn()
        {
            if (_provider != nullptr)
                _current._ai_response = _provider->GenerateResponse(_current._user_message, _current._results);
            _history.push_back(std::move(_current));
            _current = AIConversationTurn{};
            _staged_indices.clear();
            _has_pending = false;
        }

        void AIAssistantService::Reset()
        {
            _history.clear();
            _current = AIConversationTurn{};
            _staged_indices.clear();
            _has_pending = false;
            _registry = nullptr;
            _provider.reset();
        }
    }// namespace Editor
}// namespace Ailu
