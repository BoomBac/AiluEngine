#pragma once
#ifndef __AUTOMATION_CONSOLE_BRIDGE_H__
#define __AUTOMATION_CONSOLE_BRIDGE_H__

#include "Framework/Core/CoreMinimal.h"

namespace Ailu
{
    namespace Editor
    {
        class ConsoleCommandRegistry;
        class EditorAutomationRegistry;

        u32 RegisterAutomationConsoleCommands(ConsoleCommandRegistry &console_registry,
                                              EditorAutomationRegistry &automation_registry);
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_CONSOLE_BRIDGE_H__
