-- T00 Regression: Lua error handling test
-- OnUpdate deliberately throws an error to verify:
-- - Engine does not crash
-- - Error traceback is captured in log
-- - Other ScriptInstances are not affected
--
-- Flags are set as globals BEFORE error() so C++ can verify the code path was reached.

local TestError = {}

function TestError:on_create()
    engine.log("TestError:OnCreate called")
    _G["reg_err_init_called"] = true
end

function TestError:on_update(dt)
    -- Set flag BEFORE the error so C++ can verify OnUpdate was reached
    _G["reg_err_update_tried"] = true
    error("Intentional Lua error in test_error:OnUpdate for regression testing")
end

return TestError
