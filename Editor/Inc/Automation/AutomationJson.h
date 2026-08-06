#pragma once
#ifndef __AUTOMATION_JSON_H__
#define __AUTOMATION_JSON_H__

#include "Automation/AutomationTypes.h"

namespace Ailu
{
    namespace Editor
    {
        // Compact JSON codec for the automation wire types. Complex logical values
        // (color / vector / guid / references) are already represented as tagged
        // objects or arrays inside AutomationValue, so the transport carries plain
        // JSON with no additional type mapping.
        class AutomationJson
        {
        public:
            // Serialize an AutomationValue to a compact JSON string.
            static String Write(const AutomationValue &value);
            // Parse a JSON document into an AutomationValue. Returns false on malformed input.
            static bool Read(StringView text, AutomationValue &out);

            // Request / result message codec (used over the named pipe).
            static String WriteRequest(const AutomationRequest &request);
            static bool ReadRequest(StringView text, AutomationRequest &out);
            static String WriteResult(u64 request_id, const AutomationResult &result);
            static bool ReadResult(StringView text, u64 &request_id, AutomationResult &out);
        };
    }// namespace Editor
}// namespace Ailu

#endif// !__AUTOMATION_JSON_H__
