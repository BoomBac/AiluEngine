-- T03 Regression: Script-facing Entity, Transform, Scene, Time, and Input APIs.

local TestFacade = {}

function TestFacade:OnCreate()
    reg_facade_started = true
    local transform = self.entity:get_transform()
    transform:set_local_position(Vec3.new(4.0, 5.0, 6.0))
    local position = transform:get_local_position()
    reg_facade_position_ok = position.x == 4.0 and position.y == 5.0 and position.z == 6.0

    reg_facade_guid_ok = self.scene:find_entity(self.entity:get_guid()):is_valid()
    reg_facade_find_name_ok = self.scene:find_entity_by_name(self.entity:get_name()):is_valid()
    reg_facade_created_ok = self.scene:create_entity("FacadeCreated"):is_valid()
    reg_facade_time_ok = time:get_delta_time() >= 0.0 and time:get_fixed_delta_time() >= 0.0 and time:get_time() >= 0.0

    -- No Application/InputSystem exists in the standalone runner; queries must be safe.
    reg_facade_input_safe = input:is_pressed("Jump") == false and input:is_down("Jump") == false and
                            input:get_float("MoveX") == 0.0
end

return TestFacade
