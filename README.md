# AiluEngine

## Scripting Dependencies

Lua scripting is wired against vendored `sol2` and vendored `lua`.

- `Engine/Ext/sol2` is used as headers only.
- `Engine/Ext/lua` is pinned to `v5.4.8` in the superproject.
- Keep Lua on `5.4.x` unless `sol2` is updated in lockstep. The current vendored `sol2` does not support Lua `5.5.x`.