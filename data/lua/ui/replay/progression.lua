-- Native TU19 widgets still own their presentation and requirements. Supply the
-- server account's earned inventory, weapon XP and challenge progress.
if not ReplayOnline or not ReplayOnline.Enabled() then return end

local function owns(controller, id)
    return type(ReplayOnline.OwnsItem) == "function" and
        ReplayOnline.OwnsItem(controller or 0, tonumber(id) or 0)
end

for _, name in ipairs({"PROGRESSION", "LOOT"}) do
    local namespace = rawget(_G, name)
    if namespace and type(namespace.IsUnlockedWithID) == "function" then
        local unlocked = namespace.IsUnlockedWithID
        namespace.IsUnlockedWithID = function(controller, id, ...)
            if owns(controller, id) then return true end
            return unlocked(controller, id, ...)
        end
    end
end
if Gunsmith and type(Gunsmith.BEDFIJHAB) == "function" then
    local weaponData = Gunsmith.BEDFIJHAB
    Gunsmith.BEDFIJHAB = function(controller, weapon, ...)
        local native = weaponData(controller, weapon, ...)
        local xp, rank = ReplayOnline.WeaponProgression(controller or 0, weapon)
        xp, rank = tonumber(xp), tonumber(rank)
        if xp == nil or rank == nil then return native end
        local data = {}
        if type(native) == "table" then
            for key, value in pairs(native) do data[key] = value end
        end
        data.xp, data.rank = xp, rank
        return data
    end
end
ReplayOnline.Log("earned rank inventory and native weapon progression connected")

if type(ReplayOnline.ChallengeProgress) ~= "function" then return end
local function progress(controller, id)
    local value = ReplayOnline.ChallengeProgress(controller or 0, tonumber(id) or 0)
    return tonumber(value)
end
if OnlineProgression and type(OnlineProgression.BHFJJDBEHG) == "function" then
    local native = OnlineProgression.BHFJJDBEHG
    OnlineProgression.BHFJJDBEHG = function(controller, id, ...)
        local value = progress(controller, id)
        if value ~= nil then return value end
        return native(controller, id, ...)
    end
end
local function assignments(controller, kind)
    local result = {}
    for id, value in string.gmatch(ReplayOnline.ChallengeAssignments(controller or 0, kind) or "", "(%d+):(%d+),") do
        result[id] = tonumber(value)
    end
    return result
end
if ScheduledChallenges then
    ScheduledChallenges.DCIDHEEHJA = function(c) return assignments(c, "daily_mp") end
    ScheduledChallenges.JCAAACBBI = function(c) return assignments(c, "daily_br") end
    ScheduledChallenges.EBFEIJBCEC = function(c) return assignments(c, "weekly_mp") end
    ScheduledChallenges.BCFEBEHEBI = function(c) return assignments(c, "weekly_br") end
end
if Quests and type(Quests.CCBJIDFAHE) == "function" then
    local native = Quests.CCBJIDFAHE
    Quests.CCBJIDFAHE = function(controller, id, ...)
        local value = progress(controller, id)
        if value ~= nil then return value end
        return native(controller, id, ...)
    end
end
if Gunsmith and type(Gunsmith.CEFIIHJEFG) == "function" then
    local native = Gunsmith.CEFIIHJEFG
    Gunsmith.CEFIIHJEFG = function(controller, id, ...)
        local original = native(controller, id, ...)
        local value = progress(controller, id)
        if value == nil then return original end
        local data = {}
        if type(original) == "table" then for k, v in pairs(original) do data[k] = v end end
        data.progress = value
        return data
    end
end
ReplayOnline.Log("native-event daily and weapon challenge progress connected")

-- Feed the existing TU19 splash widgets, preserving their native sound, icon,
-- localization, animations and SplashManager ordering. No global event hook or
-- HUD traversal is needed; each live splash owns one low-frequency queue timer.
if type(ReplayOnline.NextProgressionNotice) ~= "function" or type(ReplayOnline.AckProgressionNotice) ~= "function" or not MenuBuilder or
    not MenuBuilder.m_types or not Engine or Engine.DDJFBBJAIG() then return end
for _, entry in ipairs({{"rankupsplash", 0}, {"challengecompletedsplash", 1}}) do
    local name, kind = entry[1], entry[2]
    local build = MenuBuilder.m_types[name]
    if type(build) == "function" then
        MenuBuilder.m_types[name] = function(parent, options)
            local widget = build(parent, options)
            if widget._replayProgressionTimer then return widget end
            local controller = (options and options.controllerIndex) or widget:getRootController()
            if controller ~= 0 then return widget end
            local failedSequence = nil
            local poll = "replay_earned_splash_" .. tostring(kind)
            widget:addEventHandler(poll, function(self)
                local sequence, eventName, value, weapon, itemList = ReplayOnline.NextProgressionNotice(controller, kind)
                if not sequence or sequence == failedSequence then return end
                local event = {name = eventName, controller = controller, rank = tonumber(value), id = tonumber(value)}
                if weapon and weapon ~= "" then event.weapon_ref = weapon end
                if itemList and itemList ~= "" then
                    event.items = {}
                    for id in string.gmatch(itemList, "(%d+),") do event.items[#event.items + 1] = tonumber(id) end
                end
                local okay, errorText = pcall(self.processEvent, self, event)
                if okay then
                    ReplayOnline.AckProgressionNotice(controller, sequence)
                    ReplayOnline.Log("earned native splash: " .. eventName .. " #" .. sequence)
                else
                    -- Preserve the pending reward notification for a subsequent
                    -- HUD, but do not flood this VM with repeated failing events.
                    failedSequence = sequence
                    ReplayOnline.Log("earned splash deferred: " .. tostring(errorText))
                end
            end)
            widget._replayProgressionTimer = LUI.UITimer.new({interval = 500, event = poll})
            widget:addElement(widget._replayProgressionTimer)
            ReplayOnline.Log("native earned splash consumer ready: " .. name)
            return widget
        end
    else
        ReplayOnline.Log("native earned splash builder unavailable: " .. name)
    end
end
