-- T07 Regression: Physics2D collision/trigger callbacks.
-- Each entity appends its physics callback invocations into the global
-- `physics_cb` table keyed by entity name, so C++ can verify routing.
local TestPhysicsCallback = {}

function TestPhysicsCallback:on_create()
    _G.physics_cb = _G.physics_cb or {}
    local name = self.entity.name
    _G.physics_cb[name] = _G.physics_cb[name] or {}

    local collider = self.entity.collider2d
    if collider == nil then
        return
    end
    collider:on_collision_enter(CollisionChannel2D.WorldDynamic, function(other, hit)
        local rec = _G.physics_cb[self.entity.name]
        rec.collision_enter = (rec.collision_enter or 0) + 1
        rec.collision_other = other.name
        if hit ~= nil then
            rec.hit_x = hit.point.x
            rec.hit_y = hit.point.y
            rec.hit_nx = hit.normal.x
            rec.hit_ny = hit.normal.y
            rec.self_shape = hit.self_shape
            rec.other_shape = hit.other_shape
        end
    end)
    collider:on_collision_exit(CollisionChannel2D.WorldDynamic, function(other, hit)
        local rec = _G.physics_cb[self.entity.name]
        rec.collision_exit = (rec.collision_exit or 0) + 1
        rec.collision_exit_other = other.name
    end)
    collider:on_trigger_enter(CollisionChannel2D.WorldDynamic, function(other)
        local rec = _G.physics_cb[self.entity.name]
        rec.trigger_enter = (rec.trigger_enter or 0) + 1
        rec.trigger_other = other.name
    end)
    collider:on_trigger_exit(CollisionChannel2D.WorldDynamic, function(other)
        local rec = _G.physics_cb[self.entity.name]
        rec.trigger_exit = (rec.trigger_exit or 0) + 1
        rec.trigger_exit_other = other.name
    end)

    collider:on_collision_enter(CollisionChannel2D.Enemy, function(other)
        local rec = _G.physics_cb[self.entity.name]
        rec.filtered_collision_enter = (rec.filtered_collision_enter or 0) + 1
        rec.filtered_collision_other = other.name
    end)
    collider:on_collision_exit(CollisionChannel2D.Enemy, function(other)
        local rec = _G.physics_cb[self.entity.name]
        rec.filtered_collision_exit = (rec.filtered_collision_exit or 0) + 1
    end)
    collider:on_trigger_enter(CollisionChannel2D.Enemy, function(other)
        local rec = _G.physics_cb[self.entity.name]
        rec.filtered_trigger_enter = (rec.filtered_trigger_enter or 0) + 1
    end)
    collider:on_trigger_exit(CollisionChannel2D.Enemy, function(other)
        local rec = _G.physics_cb[self.entity.name]
        rec.filtered_trigger_exit = (rec.filtered_trigger_exit or 0) + 1
    end)
end

return TestPhysicsCallback
