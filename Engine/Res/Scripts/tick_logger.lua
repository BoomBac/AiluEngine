local tick_logger = {}

function tick_logger:OnCreate()
    engine.log("tick_logger:OnCreate called")
    local pos = self.entity:get_position()
    self.base_x = pos[1]
    self.base_y = pos[2]
    self.base_z = pos[3]
end

function tick_logger:OnUpdate(delta_time)
    local t = engine.time()
    local y = (math.sin(t) + 1) * 6
    self.entity:set_position(self.base_x, y, self.base_z)
end

function tick_logger:OnDestroy()
    engine.log("tick_logger:OnDestroy called")
end

return tick_logger
