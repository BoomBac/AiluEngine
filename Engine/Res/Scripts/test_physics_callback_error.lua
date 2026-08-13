-- T07 Regression: faulted script must not receive physics callbacks.
-- OnCreate intentionally faults so C++ can verify DispatchPhysicsContact skips it.
local TestPhysicsCallbackError = {}

function TestPhysicsCallbackError:on_create()
    error("Intentional fault in test_physics_callback_error:OnCreate")
end

function TestPhysicsCallbackError:on_collision_enter(other, hit)
    _G.physics_cb = _G.physics_cb or {}
    _G.physics_cb[self.entity.name] = _G.physics_cb[self.entity.name] or {}
    local rec = _G.physics_cb[self.entity.name]
    rec.collision_enter = (rec.collision_enter or 0) + 1
end

return TestPhysicsCallbackError
