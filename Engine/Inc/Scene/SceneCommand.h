#pragma once
#ifndef __SCENE_COMMAND_H__
#define __SCENE_COMMAND_H__

#include "Entity.h"
#include "Component.h"
#include "Scene.h"

namespace Ailu
{
    namespace SceneManagement
    {
        class Scene;

        /// <summary>
        /// 场景命令基类，支持 undo/redo
        /// </summary>
        class AILU_API ISceneCommand
        {
        public:
            virtual ~ISceneCommand() = default;
            virtual bool Execute(Scene &scene) = 0;
            virtual bool Undo(Scene &scene) = 0;
            virtual const String &ToString() const = 0;
        };

        /// <summary>
        /// 重设父级命令
        /// </summary>
        class AILU_API ReparentSceneCommand final : public ISceneCommand
        {
        public:
            ReparentSceneCommand(ECS::Entity child, ECS::Entity new_parent, bool keep_world_transform = true);
            bool Execute(Scene &scene) final;
            bool Undo(Scene &scene) final;
            const String &ToString() const final;

        private:
            bool Apply(Scene &scene, ECS::Entity parent, const Transform &local_transform) const;
            void CaptureOldState(Scene &scene);
            void CaptureNewState(Scene &scene);

        private:
            ECS::Entity _child = ECS::kInvalidEntity;
            ECS::Entity _new_parent = ECS::kInvalidEntity;
            ECS::Entity _old_parent = ECS::kInvalidEntity;
            Transform _old_local_transform;
            Transform _new_local_transform;
            bool _keep_world_transform = true;
            bool _has_executed = false;
        };

        /// <summary>
        /// 移除组件命令（模板），支持 undo/redo
        /// </summary>
        template<typename T>
        class RemoveComponentCommand : public ISceneCommand
        {
        public:
            explicit RemoveComponentCommand(ECS::Entity entity)
                : _entity(entity)
            {
            }

            bool Execute(Scene &scene) override
            {
                if (_has_executed)
                    return false;
                auto *comp = scene.GetRegister().template GetComponent<T>(_entity);
                if (!comp)
                    return false;
                _saved_data = *comp;
                scene.GetRegister().template RemoveComponent<T>(_entity);
                _has_executed = true;
                return true;
            }

            bool Undo(Scene &scene) override
            {
                if (!_has_executed)
                    return false;
                scene.GetRegister().template AddComponent<T>(_entity, _saved_data);
                _has_executed = false;
                return true;
            }

            const String &ToString() const override
            {
                static String s = "RemoveComponent";
                return s;
            }

        private:
            ECS::Entity _entity = ECS::kInvalidEntity;
            T _saved_data{};
            bool _has_executed = false;
        };

    }// namespace SceneManagement
}// namespace Ailu

#endif// !__SCENE_COMMAND_H__
