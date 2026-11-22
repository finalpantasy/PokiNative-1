-- Pokemon Compass (v2.1.1.1): stand-ins for the six Lua globals its exefs plugin (subsdk9, "compass_exlaunch",
-- program/hooks/lua_hooks.cpp) registers in each Lua state. The plugin is Switch code and cannot run in the
-- recompiled game, so these run instead: TOTK_LUA_PRELUDE=<this file> (runtime/lua_mod.c) runs it in every new Lua
-- state just before the game's main script. Decoded from the plugin's AArch64 code (docs/compass-exefs.md).
--
-- Four are exact (seed generator and decoder, two presence checks). S1C95.. and SDE64.. switch the plugin's hook at
-- main+0x1141ba0, which the runtime's patch set compass does here (runtime/compass.c).

-- One log line per kind of call (Lua print reaches the game log through svc OutputDebugString).
local said = {}
local function say(key, msg)
    if not said[key] then
        said[key] = true
        print("compass prelude: " .. msg)
    end
end

local B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

local function b64enc(bytes)
    local out, val, valb = {}, 0, -6
    for _, c in ipairs(bytes) do
        val = ((val << 8) | c) & 0xffffffff
        valb = valb + 8
        while valb >= 0 do
            local i = ((val >> valb) & 63) + 1
            out[#out + 1] = B64:sub(i, i)
            valb = valb - 6
        end
    end
    if valb > -6 then
        local i = (((val << 8) >> (valb + 8)) & 63) + 1
        out[#out + 1] = B64:sub(i, i)
    end
    while #out % 4 ~= 0 do out[#out + 1] = "=" end
    return table.concat(out)
end

local function b64dec(s)
    local out, val, valb = {}, 0, -8
    for ch in s:gmatch(".") do
        if ch == "=" then break end
        local i = B64:find(ch, 1, true)
        if i then
            val = ((val << 6) | (i - 1)) & 0xffffffff
            valb = valb + 6
            if valb >= 0 then
                out[#out + 1] = (val >> valb) & 0xff
                valb = valb - 8
            end
        end
    end
    return out
end

-- The plugin's C function returns 1 without pushing: Lua gets the top stack slot, its last argument.
local function last(...)
    local k = select("#", ...)
    if k > 0 then return (select(k, ...)) end
end

-- S1C95BFA15FD0AD5D / SDE64977E8D1E8DB4: the plugin sets a flag and hooks main+0x1141ba0 (W0 forced to 0, or W0 =
-- c1A1CBE3B.fFCE06E04():f62782BA1()). The runtime does the same at that place (patch set compass, runtime/compass.c);
-- this line tells it which (Lua print reaches the runtime as a debug string).
function S1C95BFA15FD0AD5D(...)
    print("KARPUR_CMD compass h7 zero")
    return last(...)
end
function SDE64977E8D1E8DB4(...)
    print("KARPUR_CMD compass h7 query")
    return last(...)
end

-- S4CE0FA575C58E670: a new trainer seed. 128 keys, each a 10-bit record (key << 3 | value) after a count byte, Base64.
function S4CE0FA575C58E670()
    local bytes, acc, bits = {128}, 0, 0
    for key = 0, 127 do
        local v
        if key == 60 or key == 61 or key == 70 then v = math.random(0, 1)
        elseif key == 65 then v = math.random(0, 4)
        elseif key <= 81 then v = math.random(0, 2)
        else v = math.random(0, 7) end
        acc = (acc << 10) | ((key & 0x7f) << 3) | v
        bits = bits + 10
        while bits >= 8 do
            bits = bits - 8
            bytes[#bytes + 1] = (acc >> bits) & 0xff
        end
        acc = acc & ((1 << bits) - 1)
    end
    if bits > 0 then bytes[#bytes + 1] = (acc << (8 - bits)) & 0xff end
    local seed = b64enc(bytes)
    say("seed", "new trainer seed " .. seed:sub(1, 24) .. "... (" .. #seed .. " chars)")
    return seed
end

-- SCD77E49FEB2D092C(seed): the seed's records as a table {[key] = value} (key 0 included).
function SCD77E49FEB2D092C(s)
    local b, t = b64dec(tostring(s)), {}
    if #b == 0 then return t end
    local count, acc, bits, pos = b[1], 0, 0, 2
    for _ = 1, count do
        while bits < 10 do
            if pos > #b then return t end
            acc = (acc << 8) | b[pos]
            pos = pos + 1
            bits = bits + 8
        end
        bits = bits - 10
        local rec = (acc >> bits) & 0x3ff
        acc = acc & ((1 << bits) - 1)
        t[(rec >> 3) & 0x7f] = rec & 7
    end
    local n = 0
    for _ in pairs(t) do n = n + 1 end
    say("decode", "trainer seed decoded: " .. n .. " settings, key 65 = " .. tostring(t[65]))
    return t
end

-- S7E2857F9FF52F7EA(a, code): true when compass.GetString("Compass_TrainerSeed") is a non-empty string, else an
-- error table that sends the game back to its boot scene.
function S7E2857F9FF52F7EA(...)
    if select("#", ...) ~= 2 then return false end
    local code = select(2, ...)
    code = (type(code) == "string" or type(code) == "number") and tostring(code) or nil
    local err = {status = "error", errorCode = code, bootScene = true}
    local c = rawget(_G, "compass")
    if type(c) ~= "table" or c.GetString == nil then return err end
    local ok, s = pcall(c.GetString, "Compass_TrainerSeed")
    if ok and (type(s) == "string" or type(s) == "number") and tostring(s) ~= "" then
        say("seedok", "trainer seed check passed (Compass_TrainerSeed set)")
        return true
    end
    say("seedbad", "trainer seed check failed (code " .. tostring(code) .. "): the game goes back to its boot scene")
    return err
end

-- S0C1D2E3F4A5B6789(...): presence check; returns its own last n arguments, n = compass.Yinsight().
function S0C1D2E3F4A5B6789(...)
    local n = math.tointeger(compass.Yinsight()) or 0
    say("yinsight", "compass.Yinsight presence check passed (" .. n .. ")")
    local k = select("#", ...)
    if n <= 0 or k == 0 then return end
    if n > k then n = k end
    return select(k - n + 1, ...)
end
