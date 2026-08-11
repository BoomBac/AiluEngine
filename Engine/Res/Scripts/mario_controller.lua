-- Simple side-scrolling controller for a Mario-like character.
--
-- Required actions in the InputActionAsset:
--   Move   : Value / Axis1D, A = -1, D = +1
--   Jump   : Button, for example Space
--   Attack : Button, for example J
--
-- The entity should have a 2D rigid body and collider. Gravity is handled by
-- the physics world; this script only changes horizontal velocity and jumps.

local script = {}

local k_move_speed = 6.0
local k_jump_speed = 10.0
local k_ground_y = 0.0
local k_ground_epsilon = 0.05
local k_camera_follow_speed = 8.0

local function move_towards(current, target, max_delta)
    if current < target then
        return math.min(current + max_delta, target)
    end
    return math.max(current - max_delta, target)
end

function script:OnCreate()
    assert(input:load_action_asset("project://NewInputActions.alasset"))
    assert(input:push_context("Gameplay"))

    self._jump_requested = false
    self._attack_subscription = input:on_performed("Attack", function()
        engine.log("attack")
    end)

    self._jump_subscription = input:on_performed("Jump", function()
        self._jump_requested = true
    end)

    self._move_subscription = input:on_value_changed("Move", function(value)
        -- Move is polled in OnUpdate because an Axis1D value must be applied
        -- every frame while the key is held.
        self._last_move_value = value
    end)

    assert(physics2d:is_valid_body(self.entity), "Mario requires a 2D rigid body")
end

function script:OnDestroy()
    if self._attack_subscription then
        input:off(self._attack_subscription)
    end
    if self._jump_subscription then
        input:off(self._jump_subscription)
    end
    if self._move_subscription then
        input:off(self._move_subscription)
    end
end

function script:OnUpdate(delta_time)
    local move_axis = input:get_float("Move")
    local position = physics2d:get_position(self.entity)
    local velocity = physics2d:get_linear_velocity(self.entity)

    velocity.x = move_towards(velocity.x, move_axis * k_move_speed, 30.0 * delta_time)

    local grounded = position.y <= k_ground_y + k_ground_epsilon and velocity.y <= 0.0
    if grounded and self._jump_requested then
        velocity.y = k_jump_speed
    end
    self._jump_requested = false

    physics2d:set_linear_velocity(self.entity, velocity)

    local main_camera = self.scene:get_main_camera()
    if main_camera and main_camera:is_valid() then
        local camera_position = main_camera:get_position()
        local target_x = position.x
        camera_position.x = move_towards(camera_position.x, target_x, k_camera_follow_speed * delta_time)
        main_camera:set_position(camera_position)
    end
end

return script
