-- T00 Regression: Script lifecycle test
-- Records OnCreate, OnFixedUpdate, OnUpdate, OnLateUpdate, OnDestroy
-- call order as simple strings in the global lifecycle_record table.
-- C++ side reads lifecycle_record via RunString/GetGlobalInt.

lifecycle_record = {}

local TestLifecycle = {}

function TestLifecycle:on_create()
    table.insert(lifecycle_record, "OnCreate")
end

function TestLifecycle:on_fixed_update(dt)
    table.insert(lifecycle_record, "OnFixedUpdate")
end

function TestLifecycle:on_update(dt)
    table.insert(lifecycle_record, "OnUpdate")
end

function TestLifecycle:on_late_update(dt, alpha)
    table.insert(lifecycle_record, "OnLateUpdate")
end

function TestLifecycle:on_destroy()
    table.insert(lifecycle_record, "OnDestroy")
end

return TestLifecycle
