return {
    on_create = function(self)
        input.on_performed("Spawn", function()
            reg_input_subscription_count = reg_input_subscription_count + 1
        end)
    end
}
