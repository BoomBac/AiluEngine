-- T00 Regression: Script lifecycle test
-- Records OnInit, OnFixedUpdate, OnUpdate, OnLateUpdate, OnDestroy
-- call order as simple strings in the global lifecycle_record table.
-- C++ side reads lifecycle_record via RunString/GetGlobalInt.

lifecycle_record = {}

local TestLifecycle = {}

function TestLifecycle:OnInit()
    table.insert(lifecycle_record, "OnInit")
end

function TestLifecycle:OnFixedUpdate(dt)
    table.insert(lifecycle_record, "OnFixedUpdate")
end

function TestLifecycle:OnUpdate(dt)
    table.insert(lifecycle_record, "OnUpdate")
end

function TestLifecycle:OnLateUpdate(dt, alpha)
    table.insert(lifecycle_record, "OnLateUpdate")
end

function TestLifecycle:OnDestroy()
    table.insert(lifecycle_record, "OnDestroy")
end

return TestLifecycle
