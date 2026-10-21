-- TU19's ten-slot schema is initialized in two stages. StatsInit initializes
-- 1-5; the fetched-stats migration (EXE RVA15C80B0) runs
-- mp/stats_init_new_loadouts.cfg for 6-10. Local profile restore bypasses that
-- fetch callback. Apply the same stock defaults only to still-empty slots.
-- Do not rerun the whole cfg: it overwrites previously edited extra classes.
if not ReplayOnline.Enabled() or not PlayerData or not LOADOUT or not CoD then return end

local defaults = {
    {"iw8_ar_kilo433", "iw8_pi_mike1911", "none", 0, "equip_frag", "specialty_hustle", "specialty_hardline", "specialty_warhead"},
    {"iw8_sm_augolf", "iw8_pi_golf21", "iw8_pi_golf21", 1, "equip_frag", "specialty_eod", "specialty_hardline", "specialty_warhead"},
    {"iw8_sh_romeo870", "iw8_la_gromeo", "iw8_la_gromeo", 0, "equip_frag", "specialty_hustle", "specialty_hardline", "specialty_tune_up"},
    {"iw8_lm_pkilo", "iw8_pi_mike1911", "iw8_pi_mike1911", 0, "equip_claymore", "specialty_eod", "specialty_restock", "specialty_warhead"},
    {"iw8_sn_delta", "iw8_pi_golf21", "iw8_pi_golf21", 0, "equip_claymore", "specialty_hustle", "specialty_hardline", "specialty_tune_up"}
}
local checked = {}

local function empty(slot)
    local used = slot.inUse:get()
    if used ~= 0 and used ~= false then return false end
    -- Preserve partially edited slots, even if their inUse flag was not saved.
    for i = 0, #slot.weaponSetups - 1 do
        if slot.weaponSetups[i].weapon:get() ~= "none" then return false end
    end
    for i = 0, #slot.equipmentSetups - 1 do
        if slot.equipmentSetups[i].equipment:get() ~= "none" then return false end
    end
    for i = 0, #slot.loadoutPerks - 1 do
        if slot.loadoutPerks[i]:get() ~= "specialty_null" then return false end
    end
    return true
end

local function initialize(controller)
    if controller ~= 0 or not Engine.DDJFBBJAIG() then return end
    for _, group in ipairs({CoD.StatsGroup.RankedLoadouts, CoD.StatsGroup.PrivateLoadouts}) do
        if not checked[group] then
            local slots = PlayerData.BFFBGAJGD(controller, group).squadMembers.loadouts
            if #slots == 10 then
                local changes, count = {}, 0
                local function set(field, value)
                    -- Validate every field/getter before the first write. Retain
                    -- old values so a native setter error can roll back this batch.
                    assert(field and type(field.set) == "function")
                    changes[#changes + 1] = {field, value, field:get()}
                end
                for i = 5, #slots - 1 do
                    local slot, spec = slots[i], defaults[i - 4]
                    if empty(slot) then
                        for j = 0, 2 do set(slot.weaponSetups[j].weapon, spec[j + 1]) end
                        set(slot.overkill, spec[4])
                        set(slot.equipmentSetups[0].equipment, spec[5])
                        set(slot.equipmentSetups[1].equipment, "equip_flash")
                        for j = 0, 2 do set(slot.loadoutPerks[j], spec[j + 6]) end
                        set(slot.name, Engine.CBBHFCGDIC("LUA_MENU_MP/LOADOUT_" .. (i + 1)))
                        set(slot.inUse, 1)
                        count = count + 1
                    end
                end
                local written = 0
                local ok, err = pcall(function()
                    for i, change in ipairs(changes) do
                        written = i
                        change[1]:set(change[2])
                    end
                end)
                if not ok then
                    for i = written, 1, -1 do
                        local change = changes[i]
                        local restored, why = pcall(change[1].set, change[1], change[3])
                        if not restored then ReplayOnline.Log("loadout rollback failed: " .. tostring(why)) end
                    end
                    error(err)
                end
                checked[group] = true
                if count > 0 then
                    ReplayOnline.Log("native extra loadouts initialized: group=" .. group .. " count=" .. count)
                end
            end
        end
    end
end

local reported = false
local function prepare(controller)
    local ok, err = pcall(initialize, controller)
    if not ok and not reported then
        reported = true
        ReplayOnline.Log("extra loadout initialization deferred: " .. tostring(err))
    end
end

local preparePlayerData = ReplayOnline.PreparePlayerData
ReplayOnline.PreparePlayerData = function(...)
    local ready = preparePlayerData(...)
    if ready then prepare(0) end
    return ready
end
if MenuBuilder and MenuBuilder.m_types and type(MenuBuilder.m_types.loadoutselect) == "function" then
    local build = MenuBuilder.m_types.loadoutselect
    MenuBuilder.m_types.loadoutselect = function(menu, options, ...)
        prepare(options and options.controllerIndex or Engine.IJEBHJIJF())
        return build(menu, options, ...)
    end
end
