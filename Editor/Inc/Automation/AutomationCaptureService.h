#pragma once
#ifndef __AUTOMATION_CAPTURE_SERVICE_H__
#define __AUTOMATION_CAPTURE_SERVICE_H__

#include "Automation/AutomationRegistry.h"

namespace Ailu
{
    namespace Editor
    {
        // 注册抓帧相关工具，用于让外部（含 AI 助手）直接取到某个 render graph pass 的输出图：
        //   render.capture_pass        抓取指定 pass 的输出/输入若干帧，落盘为 PNG
        //   render.capture_status      查询会话状态与已落盘文件
        //   render.set_feature_active  开关 render feature（RayTraceGI 只有激活后才注册 pass）
        //   render.list_features       列出 render feature 及其激活状态
        class AutomationCaptureService final : public NonCopyable
        {
        public:
            static void Register(EditorAutomationRegistry &registry);

        private:
            static AutomationResult HandleCapturePass(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleCaptureStatus(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleSetFeatureActive(EditorAutomationContext &context, const AutomationObject &arguments);
            static AutomationResult HandleListFeatures(EditorAutomationContext &context, const AutomationObject &arguments);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_CAPTURE_SERVICE_H__
