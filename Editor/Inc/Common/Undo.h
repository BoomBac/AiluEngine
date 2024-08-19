//
// Created by 22292 on 2024/7/13.
//

#ifndef AILU_UNDO_H
#define AILU_UNDO_H

#include <stack>
#include "Objects/TransformComponent.h"
#include "Framework/Common/Log.h"

namespace Ailu
{
    namespace Editor
    {
        class ICommand
        {
        public:
            virtual ~ICommand() {}
            virtual void Execute() = 0;
            virtual void Undo() = 0;
            virtual const String& ToString() const = 0;
        };
#define DECLARE_COMMAND(name) \
public:\
        [[nodiscard]] const String& ToString() const final {return s_name;} \
        private:inline static String s_name = #name;

        class TransformCommand : public ICommand
        {
            DECLARE_COMMAND(Transform)
        public:
            TransformCommand(TransformComponent* comp, const Transform& old_transf)
                : _comp(comp), _old_transf(old_transf),_new_transf(comp->SelfTransform()) {}

            void Execute() override
            {
                _comp->SetPosition(_new_transf._position);
                _comp->SetRotation(_new_transf._rotation);
                _comp->SetScale(_new_transf._scale);
                g_pLogMgr->LogFormat("Exe or redo {}",s_name);
            }

            void Undo() override
            {
                _comp->SetPosition(_old_transf._position);
                _comp->SetRotation(_old_transf._rotation);
                _comp->SetScale(_old_transf._scale);
                g_pLogMgr->LogFormat("Undo {}",s_name);
            }
        private:
            Transform _old_transf;
            Transform _new_transf;
            TransformComponent* _comp;
        };

        class Undo
        {

        };
        class CommandManager
        {
        public:
            void ExecuteCommand(std::unique_ptr<ICommand> command)
            {
                command->Execute();
                m_UndoStack.push(std::move(command));
                // Clear the redo stack
                while (!m_RedoStack.empty())
                    m_RedoStack.pop();
            }

            void Undo()
            {
                if (!m_UndoStack.empty())
                {
                    auto command = std::move(m_UndoStack.top());
                    m_UndoStack.pop();
                    command->Undo();
                    m_RedoStack.push(std::move(command));
                }
            }

            void Redo()
            {
                if (!m_RedoStack.empty())
                {
                    auto command = std::move(m_RedoStack.top());
                    m_RedoStack.pop();
                    command->Execute();
                    m_UndoStack.push(std::move(command));
                }
            }

        private:
            std::stack<std::unique_ptr<ICommand>> m_UndoStack;
            std::stack<std::unique_ptr<ICommand>> m_RedoStack;
        };
        extern CommandManager* g_pCommandMgr;
    }// namespace Editor

}


#endif//AILU_UNDO_H
