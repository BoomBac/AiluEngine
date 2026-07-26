//
// Created by 22292 on 2024/7/13.
//

#ifndef AILU_UNDO_H
#define AILU_UNDO_H

#include "Framework/Common/Log.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"
#include "Scene/SceneCommand.h"
#include <stack>

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
            virtual const String &ToString() const = 0;
        };
#define DECLARE_COMMAND(name)                                             \
public:                                                                   \
    [[nodiscard]] const String &ToString() const final { return s_name; } \
                                                                          \
private:                                                                  \
    inline static String s_name = #name;

        class TransformCommand : public ICommand
        {
            DECLARE_COMMAND(Transform)
        public:
            TransformCommand(Vector<String> &&obj_names, Vector<ECS::TransformComponent *> &&comps, Vector<Transform> &&old_transforms)
                : _obj_names(obj_names), _comps(comps), _old_transforms(old_transforms)
            {
                for (auto *comp: _comps)
                {
                    _new_transforms.push_back(comp->_local_transform);
                }
            }
            TransformCommand(const String &obj_name,ECS::TransformComponent *comp, const Transform &old_transf)
            {
                _obj_names.emplace_back(obj_name);
                _comps.emplace_back(comp);
                _old_transforms.emplace_back(old_transf);
                _new_transforms.emplace_back(comp->_local_transform);
            }

            void Execute() override
            {
                for (size_t i = 0; i < _comps.size(); ++i)
                {
                    _comps[i]->_local_transform._position = _new_transforms[i]._position;
                    _comps[i]->_local_transform._rotation = _new_transforms[i]._rotation;
                    _comps[i]->_local_transform._scale = _new_transforms[i]._scale;
                    LOG_INFO("Exe or redo {} on obj {}", s_name, _obj_names[i]);
                }
            }

            void Undo() override
            {
                for (size_t i = 0; i < _comps.size(); ++i)
                {
                    _comps[i]->_local_transform._position = _old_transforms[i]._position;
                    _comps[i]->_local_transform._rotation = _old_transforms[i]._rotation;
                    _comps[i]->_local_transform._scale = _old_transforms[i]._scale;
                    LOG_INFO("Undo {} on obj {}", s_name, _obj_names[i]);
                }
            }

        private:
            std::vector<String> _obj_names;
            std::vector<ECS::TransformComponent *> _comps;
            std::vector<Transform> _old_transforms;
            std::vector<Transform> _new_transforms;
        };


        class SceneQueuedCommand : public ICommand
        {
        public:
            SceneQueuedCommand(SceneManagement::Scene *scene, Scope<SceneManagement::ISceneCommand> command)
                : _scene(scene), _command(std::move(command))
            {
            }

            void Execute() override
            {
                if (_scene != nullptr && _command != nullptr)
                    _scene->EnqueueSceneCommand(_command.get(), false);
            }

            void Undo() override
            {
                if (_scene != nullptr && _command != nullptr)
                    _scene->EnqueueSceneCommand(_command.get(), true);
            }

            [[nodiscard]] const String &ToString() const override
            {
                if (_command != nullptr)
                    return _command->ToString();
                static String empty = "SceneCommand";
                return empty;
            }

        private:
            SceneManagement::Scene *_scene = nullptr;
            Scope<SceneManagement::ISceneCommand> _command;
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
                _undo_views.push_back(command.get());
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
                    _undo_views.pop_front();
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
                    _undo_views.push_back(command.get());
                    m_UndoStack.push(std::move(command));
                }
            }
            const List<ICommand *> &UndoViews() const { return _undo_views; }
        private:
            std::stack<std::unique_ptr<ICommand>> m_UndoStack;
            std::stack<std::unique_ptr<ICommand>> m_RedoStack;
            List<ICommand *> _undo_views;
        };
        extern CommandManager *g_pCommandMgr;
    }// namespace Editor

}// namespace Ailu


#endif//AILU_UNDO_H
