#pragma once
#ifndef __AUTOMATION_BATCH_REPORT_H__
#define __AUTOMATION_BATCH_REPORT_H__

#include "Framework/Core/String.h"

namespace Ailu
{
    namespace Editor
    {
        class AutomationBatchReport final
        {
        public:
            static bool WriteStartupFailure(const WString &report_path,
                                            const WString &run_id,
                                            const String &requested_tests,
                                            const String &graphics_mode,
                                            const String &error_code,
                                            const String &error_message);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_BATCH_REPORT_H__
