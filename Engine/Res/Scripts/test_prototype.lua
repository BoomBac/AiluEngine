-- T02 Regression: a shared ScriptPrototype must execute this chunk once,
-- regardless of how many entities create instances from it.

reg_prototype_top_level_count = (reg_prototype_top_level_count or 0) + 1

local TestPrototype = {}

function TestPrototype:OnCreate()
    self.value = 0
end

function TestPrototype:OnUpdate(dt)
    self.value = self.value + 1
end

return TestPrototype
