#pragma once

#include "Graph/GraphDocument.h"
#include "Graph/GraphSchema.h"

#include <functional>

namespace Ailu
{
    namespace Editor
    {
        struct GraphActionMenuContext
        {
            GraphDocument *_document = nullptr;
            const GraphPinData *_source_pin = nullptr;
        };

        class GraphActionMenu
        {
        public:
            using ActionCallback = std::function<void(const GraphNodeAction &)>;
            using CloseCallback = std::function<void()>;

            static bool ShowAt(Vector2f popup_pos, const GraphActionMenuContext &context, ActionCallback on_action,
                               CloseCallback on_close = {});
        };
    } // namespace Editor
} // namespace Ailu
