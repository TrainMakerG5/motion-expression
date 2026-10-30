local ffi = require("ffi")
ffi.cdef[[
    int ExpressionEvaluateBinding(int id, double value, double time, double frame,
        double* output, const char** error);
]]
local native = ffi.load("Expression.mod2")

return function(id, value, time, frame)
    local output = ffi.new("double[1]")
    local message = ffi.new("const char*[1]")
    if native.ExpressionEvaluateBinding(id, value, time, frame, output, message) == 0 then
        error("JSエクスプレッション: " .. ffi.string(message[0]))
    end
    return tonumber(output[0])
end
