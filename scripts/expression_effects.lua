return function(effects)
    local cursor = 1
    while cursor <= #effects do
        local name = effects[cursor]
        local count = tonumber(effects[cursor + 1])
        local arguments = {name}
        cursor = cursor + 2
        for index = 1, count do
            arguments[#arguments + 1] = effects[cursor]
            local kind, value = effects[cursor + 1], effects[cursor + 2]
            arguments[#arguments + 1] = kind == "number" and tonumber(value) or value
            cursor = cursor + 3
        end
        obj.effect(unpack(arguments))
    end
end
