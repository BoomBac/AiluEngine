/*
 * @author    : BoomBac
 * @created   : 2026.7
 * @brief     : InputActionAsset – serialisable input configuration resource
 *
 * InputActionAsset is a pure-data Object that stores action maps, contexts,
 * binding paths, processor/interaction parameters. It inherits Object for
 * engine resource management and serialization.
 *
 * IMPORTANT: Asset objects store *descriptions*, not runtime device pointers.
 * Use InputSystem::LoadAsset() to instantiate runtime state.
 */

#pragma once
#ifndef __INPUT_ACTION_ASSET_H__
#define __INPUT_ACTION_ASSET_H__

#include "InputActionMap.h"
#include "InputContext.h"
#include "Objects/Object.h"
#include "generated/InputActionAsset.gen.h"

namespace Ailu
{
    ACLASS()
    class AILU_API InputActionAsset : public Object
    {
        GENERATED_BODY()

    public:
        InputActionAsset();
        explicit InputActionAsset(const String &name);

        // --- Action Maps ---
        [[nodiscard]] InputActionMap *FindActionMap(StringView name);
        [[nodiscard]] const InputActionMap *FindActionMap(StringView name) const;

        [[nodiscard]] Vector<InputActionMap> &GetActionMaps() { return _action_maps; }
        [[nodiscard]] const Vector<InputActionMap> &GetActionMaps() const { return _action_maps; }

        void AddActionMap(const InputActionMap &action_map) { _action_maps.push_back(action_map); }
        void AddActionMap(InputActionMap &&action_map) { _action_maps.push_back(std::move(action_map)); }

        // --- Contexts ---
        [[nodiscard]] InputContext *FindContext(StringView name);
        [[nodiscard]] const InputContext *FindContext(StringView name) const;

        [[nodiscard]] Vector<InputContext> &GetContexts() { return _contexts; }
        [[nodiscard]] const Vector<InputContext> &GetContexts() const { return _contexts; }

        void AddContext(const InputContext &context) { _contexts.push_back(context); }
        void AddContext(InputContext &&context) { _contexts.push_back(std::move(context)); }

    private:
        Vector<InputActionMap> _action_maps;

        Vector<InputContext> _contexts;
    };

} // namespace Ailu

#endif // !__INPUT_ACTION_ASSET_H__
