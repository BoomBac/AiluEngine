//
// Created by 22292 on 2024/7/13.
//

#ifndef AILU_UNDO_H
#define AILU_UNDO_H

#include "Framework/Common/Log.h"
#include "Assets/Asset.h"
#include "Objects/JsonArchive.h"
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

        // Base for asset commands.  A command owns the revision transitions while
        // derived classes only apply and undo their domain-specific value changes.
        class AssetEditCommand : public ICommand
        {
        public:
            explicit AssetEditCommand(Asset *asset) : _asset(asset) {}

            void Execute() final
            {
                if (_asset == nullptr)
                    return;
                if (!_has_executed)
                {
                    _before_revision = _asset->GetRevision();
                    if (!ApplyEdit(false))
                        return;
                    _after_revision = _asset->MarkModified();
                    _has_executed = true;
                    return;
                }

                if (ApplyEdit(true))
                    _asset->RestoreRevision(_after_revision);
            }

            void Undo() final
            {
                if (_asset != nullptr && _has_executed && UndoEdit())
                    _asset->RestoreRevision(_before_revision);
            }

            Asset *GetAsset() const { return _asset; }
            Asset::Revision BeforeRevision() const { return _before_revision; }
            Asset::Revision AfterRevision() const { return _after_revision; }

        protected:
            virtual bool ApplyEdit(bool is_redo) = 0;
            virtual bool UndoEdit() = 0;

        protected:
            Asset *_asset = nullptr;
            Asset::Revision _before_revision = 0;
            Asset::Revision _after_revision = 0;
            bool _has_executed = false;
        };

        inline String CaptureAssetObject(const Asset *asset)
        {
            if (asset == nullptr || asset->_p_obj == nullptr)
                return {};
            Object *object = asset->_p_obj.get();
            JsonArchive archive;
            for (const Type *type = object->GetType(); type != nullptr && type != Object::StaticType();
                 type = type->BaseType())
            {
                for (const PropertyInfo &property : type->GetProperties())
                    property.Serialize(object, archive);
            }
            return archive.SaveToString();
        }

        inline bool ApplyAssetObjectSnapshot(Asset *asset, const String &snapshot)
        {
            if (asset == nullptr || asset->_p_obj == nullptr || snapshot.empty())
                return false;
            JsonArchive archive;
            if (!archive.LoadFromString(snapshot))
                return false;
            Object *object = asset->_p_obj.get();
            for (const Type *type = object->GetType(); type != nullptr && type != Object::StaticType();
                 type = type->BaseType())
            {
                for (const PropertyInfo &property : type->GetProperties())
                    property.Deserialize(object, archive);
            }
            return true;
        }

        class AssetSnapshotCommand final : public AssetEditCommand
        {
        public:
            AssetSnapshotCommand(Asset *asset, String before, String after, String name = "Asset Edit")
                : AssetEditCommand(asset), _before(std::move(before)), _after(std::move(after)), _name(std::move(name))
            {
            }

            const String &ToString() const final { return _name; }

        protected:
            bool ApplyEdit(bool is_redo) override
            {
                if (!is_redo && _initial_state_applied)
                {
                    _initial_state_applied = false;
                    return true;
                }
                return ApplyAssetObjectSnapshot(_asset, _after);
            }

            bool UndoEdit() override
            {
                return ApplyAssetObjectSnapshot(_asset, _before);
            }

        private:
            String _before;
            String _after;
            String _name;
            bool _initial_state_applied = true;
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
