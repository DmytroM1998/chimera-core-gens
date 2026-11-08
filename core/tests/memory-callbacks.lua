-- Use the ROM made by tools/make_callback_rom.py in a new Gens core project.
-- Chimera 06066f3 or newer. This is a sandbox witness, not a Gens Lua shim.
assert(event.availableScopes()[0] == "M68K BUS")
assert(memory.usememorydomain("M68K BUS"))
assert(not event.can_replace_value("read") and not event.can_replace_value("write"))

local executions, reads, writes, legacy = 0, 0, 0, 0
local ids = {}
local function keep(id) ids[#ids + 1] = id end
local failures = {}
local function checked(fn)
    return function(...)
        local ok, why = pcall(fn, ...)
        if not ok then failures[#failures + 1] = tostring(why) end
    end
end
local function healthy() assert(#failures == 0, table.concat(failures, "\n")) end
local insideFrame, frameExecutions = false, 0
keep(event.onframestart(checked(function()
    assert(not insideFrame, "frame start without frame end")
    insideFrame, frameExecutions = true, 0
end)))
keep(event.onframeend(checked(function()
    assert(insideFrame and frameExecutions > 0, "frame ended without execution callbacks")
    insideFrame = false
end)))

keep(event.on_bus_exec(checked(function(address, value, flags)
    assert(insideFrame, "execution outside frame events")
    assert(address == 0x224 and value == 0 and flags == 0x4000)
    assert(emu.getregister("M68K PC") == address)
    assert(emu.getregister("D3") == 0x89ABCDEF)
    assert(emu.getregister("M68K A0") == 0xFF0100)
    assert(memory.read_u32_be(0xFF0104) == 0x89ABCDEF)
    executions = executions + 1
end), 0x224, "live registers", "M68K BUS"))

keep(event.on_bus_exec_any(checked(function(address, value, flags)
    assert(insideFrame, "execution outside frame events")
    frameExecutions = frameExecutions + 1
    assert(emu.getregister("PC") == address and value == 0 and flags == 0x4000)
    if address == 0x22E then
        assert((emu.getregister("M68K SR") & 0xF) == 8)
    end
end), "all instructions"))

keep(event.on_bus_read(checked(function(address, value, flags)
    assert(insideFrame and flags == 0x21000)
    assert(address == 0xFF0104 and value == 0x89AB)
    assert(memory.read_u16_be(address) == value)
    reads = reads + 1
end), 0xFF0104, "word read"))

keep(event.on_bus_write(checked(function(address, value, flags)
    assert(insideFrame and flags == 0x22000)
    assert(address == 0xFF0106 and value == 0xCDEF)
    assert(memory.read_u16_be(address) == value)
    writes = writes + 1
end), 0xFF0106, "word write"))

keep(event.onmemoryexecute(checked(function() legacy = legacy + 1 end), 0x202, "legacy exec"))
keep(event.onmemoryexecuteany(checked(function() end), "legacy wildcard"))
keep(event.onmemoryread(checked(function(_, _, flags) assert(flags == 0x11000) end), 0xFF0100, "legacy read"))
keep(event.onmemorywrite(checked(function(_, _, flags) assert(flags == 0x12000) end), 0xFF0100, "legacy write"))

assert(not pcall(event.on_bus_exec, function() end, 0x1000000, "outside bus"))
assert(not pcall(event.on_bus_exec, function() end, 0, "wrong scope", "Z80"))

emu.frameadvance()
healthy()
assert(executions > 0 and reads > 0 and writes > 0 and legacy > 0)
local saved = memorysavestate.savecorestate()
emu.frameadvance()
healthy()
memorysavestate.loadcorestate(saved)
local before = executions
emu.frameadvance()
healthy()
assert(executions > before, "state restoration removed the watches")
memorysavestate.removestate(saved)

local function waitFor(done)
    for _ = 1, 2000 do
        healthy()
        if done() then return end
        emu.yield()
    end
    error("timed out waiting for branch operation")
end
userdata.set("Gens callback count", executions)
local branchCount = executions
client.pause()
local made = chimera.branches.made
local branch = 0
for _ in pairs(tastudio.getbranches()) do branch = branch + 1 end
chimera.branches.create("Gens callback witness")
waitFor(function() return chimera.branches.made > made end)
local loaded = false
keep(event.onloadstate(checked(function(name)
    assert(name == "Gens callback witness")
    assert(userdata.get("Gens callback count") == branchCount, "userdata was not restored before the load event")
    loaded = true
end)))
userdata.set("Gens callback count", -1)
client.unpause()
emu.frameadvance()
healthy()
client.pause()
local oldControlScheme = chimera.branches.oldControlScheme
chimera.branches.oldControlScheme = false
tastudio.loadbranch(branch)
waitFor(function() return loaded end)
chimera.branches.oldControlScheme = oldControlScheme
before = executions
client.unpause()
emu.frameadvance()
healthy()
assert(executions > before, "branch restoration removed the watches")
userdata.remove("Gens callback count")

for _, id in ipairs(ids) do assert(event.unregisterbyid(id)) end
before = executions + reads + writes + legacy
emu.frameadvance()
healthy()
assert(executions + reads + writes + legacy == before, "removed watches still fire")
print("Gens sandbox memory callbacks OK")
