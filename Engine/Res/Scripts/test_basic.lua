-- T00 Regression: Basic Lua execution test
-- Validates: engine.log, engine.time, engine.delta_time bindings,
--            RunFile table return, Entity handle APIs.
-- Instance state is mirrored to globals (keyed by entity name) for C++ verification.

engine.log("test_basic.lua loaded")

-- Global-level binding checks
test_basic_time = engine.time()
test_basic_delta_time = engine.delta_time()
test_basic_fixed_dt = engine.fixed_delta_time()
test_basic_render_alpha = engine.render_alpha()
test_basic_loaded = true

local TestBasic = {}

function TestBasic:OnInit()
    engine.log("TestBasic:OnInit called")
    self.init_called = true

    if self.entity then
        local name = self.entity:get_name()
        _G["reg_ent_init_" .. name] = true
        _G["reg_ent_valid_" .. name] = self.entity:is_valid()

        local pos = self.entity:get_transform():get_local_position()
        _G["reg_ent_pos_x_" .. name] = pos.x
        _G["reg_ent_pos_y_" .. name] = pos.y
        _G["reg_ent_pos_z_" .. name] = pos.z
    end
end

function TestBasic:OnUpdate(dt)
    engine.log("TestBasic:OnUpdate called, dt=" .. tostring(dt))
    self.update_called = true
    self.last_dt = dt

    if self.entity then
        local name = self.entity:get_name()
        _G["reg_ent_update_" .. name] = true
        _G["reg_ent_last_dt_" .. name] = dt
    end
end

return TestBasic
