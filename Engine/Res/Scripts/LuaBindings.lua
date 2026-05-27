Entity = {}
function Entity:set_position(x, y, z)
    -- This function will be bound to the C++ Entity class
end

engine = {}
function engine.time()
    -- This function will be bound to the C++ engine.time() function
end

function engine.delta_time()
    -- This function will be bound to the C++ engine.delta_time() function
end