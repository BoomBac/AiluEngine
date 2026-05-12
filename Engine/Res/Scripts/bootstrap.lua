engine.log("bootstrap.lua loaded")

local seconds = engine.time()
bootstrap_loaded = true
bootstrap_last_time = seconds
engine.log(string.format("engine.time() = %.3f", seconds))