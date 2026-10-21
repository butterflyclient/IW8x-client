-- TU19 gameplay presentation. Keep the original scoreboard and pause builders.
if not ReplayOnline.Enabled() or Engine.DDJFBBJAIG() then return end

local rankScope = 0
local menuScope = 0
local nativeBool = Dvar.IBEGCHEFE
Dvar.IBEGCHEFE = function(name, ...)
    -- ScoreboardRow hides its stock rank/icon for LAN and private matches.
    -- Only its synchronous refresh sees this presentation value; the actual
    -- dvars and native connection path retain their required values.
    if (rankScope > 0 or menuScope > 0) and (name == "LPSPMQSNPQ" or name == "LSTLQTSSRM") then
        return false
    end
    if menuScope > 0 and name == "LTSNLQNRKO" then return true end
    return nativeBool(name, ...)
end
local function restoreScope(previous, ok, ...)
    rankScope = previous
    if not ok then error((...), 0) end
    return ...
end
local function restoreMenuScope(previous, ok, ...)
    menuScope = previous
    if not ok then error((...), 0) end
    return ...
end
local function onlineMenuCall(callback, ...)
    if not ReplayOnline.InDedicatedMatch() then return callback(...) end
    local previous = menuScope
    menuScope = previous + 1
    return restoreMenuScope(previous, pcall(callback, ...))
end
local buildRow = MenuBuilder.m_types.scoreboardrow
if type(buildRow) == "function" then
    MenuBuilder.m_types.scoreboardrow = function(...)
        local row = buildRow(...)
        if not row._replayRankPresentation then
            local refresh = row.RefreshRowContent
            row.RefreshRowContent = function(self, ...)
                if not ReplayOnline.InDedicatedMatch() then return refresh(self, ...) end
                local previous = rankScope
                rankScope = previous + 1
                -- Unwind on native Lua errors, including nested row refreshes.
                return restoreScope(previous, pcall(refresh, self, ...))
            end
            row._replayRankPresentation = true
        end
        return row
    end
    ReplayOnline.Log("native online scoreboard rank presentation enabled")
end

for _, name in ipairs({"mppausemenu", "brpausemenu"}) do
    local build = MenuBuilder.m_types[name]
    if type(build) == "function" then
        MenuBuilder.m_types[name] = function(parent, options)
            -- Native public pause constructors include ChallengesSummary (MP)
            -- or ChallengesSummaryBR/DoubleXPNotifications (BR). Only their
            -- construction sees onlinegame1/systemlink0/privateMatch0; native
            -- transport and subsequent action callbacks keep real game state.
            return onlineMenuCall(build, parent, options)
        end
    end
end
ReplayOnline.Log("native public pause presentation enabled for dedicated matches")
