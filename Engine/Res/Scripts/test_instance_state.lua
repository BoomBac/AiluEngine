-- T00 Regression: Multiple instance state isolation test
-- Each Entity gets an independent Lua instance table.
-- self.counter increments per update; values are mirrored to globals
-- keyed by entity name for C++ verification.

local TestState = {}

function TestState:OnCreate()
    self.counter = 0
    self.fixed_counter = 0

    -- Mirror initial values to globals for C++ inspection
    if self.entity then
        local name = self.entity:get_name()
        _G["reg_inst_counter_" .. name] = 0
        _G["reg_inst_fixed_" .. name] = 0
    end
end

function TestState:OnUpdate(dt)
    self.counter = self.counter + 1

    if self.entity then
        local name = self.entity:get_name()
        _G["reg_inst_counter_" .. name] = self.counter
    end
end

function TestState:OnFixedUpdate(dt)
    self.fixed_counter = self.fixed_counter + 1

    if self.entity then
        local name = self.entity:get_name()
        _G["reg_inst_fixed_" .. name] = self.fixed_counter
    end
end

return TestState
