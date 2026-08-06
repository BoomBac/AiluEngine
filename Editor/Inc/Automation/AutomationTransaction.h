#pragma once
#ifndef __AUTOMATION_TRANSACTION_H__
#define __AUTOMATION_TRANSACTION_H__

#include "Automation/AutomationCommand.h"

#include <functional>

namespace Ailu
{
    namespace Editor
    {
        struct AutomationTransactionOperation
        {
            String _operation;
            String _temporary_id;
            AutomationObject _arguments;
        };

        struct AutomationTransaction
        {
            Vector<AutomationTransactionOperation> _operations;
            bool _has_expected_scene_revision = false;
            u64 _expected_scene_revision = 0u;
        };

        // Builds the concrete IEditorCommand for one transaction operation.
        using TransactionOperationBuilder = std::function<Scope<IEditorCommand>(const AutomationObject &arguments)>;

        // Executes a list of operations as one atomic, single-undo unit. Temporary
        // results ($temp_id) are substituted into later operation arguments.
        class TransactionCommand final : public IEditorCommand
        {
        public:
            TransactionCommand(AutomationTransaction transaction, HashMap<String, TransactionOperationBuilder> builders);

            EditorCommandResult Validate(EditorCommandContext &context) override;
            EditorCommandResult Execute(EditorCommandContext &context) override;
            EditorCommandResult Undo(EditorCommandContext &context) override;
            StringView Name() const override { return "Transaction"; }

        private:
            EditorCommandResult ApplyAll(EditorCommandContext &context, bool is_undo);
            Scope<IEditorCommand> Build(const AutomationTransactionOperation &operation, const AutomationObject &resolved_arguments);
            static bool IsStructural(StringView operation_name);

            AutomationTransaction _transaction;
            HashMap<String, TransactionOperationBuilder> _builders;
            HashMap<String, AutomationValue> _temp_results;
            Vector<Scope<IEditorCommand>> _executed;
        };

        // Parse the transaction from tool arguments. Returns false with an error on malformed input.
        bool ParseTransaction(const AutomationObject &arguments, AutomationTransaction &out, String &error);

        // Resolve "$temp_id" string references inside arguments.
        AutomationObject ResolveTransactionTempRefs(const AutomationObject &arguments, const HashMap<String, AutomationValue> &temp_results);

        // The standard operation -> command builder map for the known write tools.
        HashMap<String, TransactionOperationBuilder> BuildDefaultTransactionBuilders();
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_TRANSACTION_H__
