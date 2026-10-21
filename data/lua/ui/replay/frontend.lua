-- TU19 retail frontend presentation. Keep stock builders, navigation and data;
-- the gameplay VM exits before any of these adapters are installed.
if not ReplayOnline.Enabled() or not Engine.DDJFBBJAIG() then return end

-- TU19's shared private-map validator reads CP.CLASSIC_MISSIONS even in MP
-- when dev features are off. The MP requires list does not load that module.
-- Load the original native dependency, retaining its constants and validators.
-- BEGIN PRIVATE MAP DEPENDENCY
if type(PRIVATE_MATCH) == "table" and type(CP) ~= "table" then
    require("ui.utils.cp.CPUtils")
end
-- END PRIVATE MAP DEPENDENCY

local function wrap(name, after)
    local build = MenuBuilder.m_types[name]
    if type(build) ~= "function" then return end
    MenuBuilder.m_types[name] = function(parent, options)
        local element = build(parent, options)
        after(element, (options and options.controllerIndex) or Engine.IJEBHJIJF())
        return element
    end
end

-- Hide the promotional tournament playlist tile at construction, so native
-- stacked-layout sizing and focus links are built without a hidden gap/button.
-- This does not disable the backend or the separate Client Settings entry.
for _, name in ipairs({"mpplaymenubuttons", "brplaymenubuttons", "brplaymenu"}) do
    local build = MenuBuilder.m_types[name]
    if type(build) == "function" and type(CONDITIONS.IsTournamentModeAllowed) == "function" then
        MenuBuilder.m_types[name] = function(...)
            local allowed = CONDITIONS.IsTournamentModeAllowed
            local f2pAllowed = TOURNAMENT and TOURNAMENT.IsF2PTournamentAllowed
            CONDITIONS.IsTournamentModeAllowed = function() return false end
            -- The imported BR parent creates this tile after its button-list
            -- constructor returns, using the separate F2P predicate. Scope
            -- both predicates to construction; the real tournament service
            -- and its Client Settings entry keep their original functions.
            if type(f2pAllowed) == "function" then
                TOURNAMENT.IsF2PTournamentAllowed = function() return false end
            end
            local function finish(ok, ...)
                CONDITIONS.IsTournamentModeAllowed = allowed
                if type(f2pAllowed) == "function" then
                    TOURNAMENT.IsF2PTournamentAllowed = f2pAllowed
                end
                if not ok then error((...), 0) end
                return ...
            end
            return finish(pcall(build, ...))
        end
    end
end

-- Both stock search panels and public-lobby footers call this same formatter.
-- The native start reply has no retail rs_backoff.ping_cutoff schedule. Show
-- the measured backend round trip, without pretending it is a game UDP ping
-- or inventing a '<N ms' search limit. No requests run on the Lua thread.
if Lobby and type(Lobby.GetStatusString) == "function" then
    local nativeStatus = Lobby.GetStatusString
    local function countdown()
        if not IsPublicMatch() then return end
        local state, data = Lobby.BGIBDIBHH()
        local remaining
        if state == Lobby.PartyState.STARTING and data then remaining = data.timeRemaining end
        if type(ReplayOnline.MatchStartCountdown) == "function" then
            local deadline = ReplayOnline.MatchStartCountdown()
            -- Native party status can still say 'waiting for players' when
            -- this host has issued go with a lower minimum than the playlist.
            remaining = tonumber(deadline) or remaining
        end
        if type(remaining) ~= "number" or remaining ~= remaining or math.abs(remaining) == math.huge then return end
        if remaining < 0 then return Engine.CBBHFCGDIC("MENU/GAME_BEGINNING"), "starting_no_time" end
        return Engine.CBBHFCGDIC("MENU/MATCH_BEGINNING_IN", math.ceil(remaining / 1000)), "starting"
    end
    Lobby.GetStatusString = function(controller, ...)
        -- The service task can become idle while Party is still counting down.
        -- Stock GetStatusString returns an empty string for that idle state,
        -- before checking Party's actual start deadline (BGIBDIBHH/19B99A0).
        local text, state = countdown()
        if text then return text, false, state end
        local text, animate, state = nativeStatus(controller, ...)
        if state == "searching" and type(ReplayOnline.MatchmakingPing) == "function" then
            local sample = ReplayOnline.MatchmakingPing()
            local ping = tonumber(sample)
            if ping and ping >= 0 and ping <= 60000 then
                return Engine.CBBHFCGDIC("MPUI/MMING_SEARCHING") .. string.format(" - backend ping %d ms", math.ceil(ping)), animate, state
            end
        end
        return text, animate, state
    end
    for _, name in ipairs({"mppubliclobby", "brpubliclobby"}) do
        wrap(name, function(menu)
            local footer = menu.LobbyStatus
            if not footer then return end
            local forced = false
            local function update()
                if countdown() then
                    -- searching_for_lobby may have hidden this footer before
                    -- native Party's start state arrived. Its existing 500ms
                    -- timer drives this update; keep its text/audio native.
                    forced = true
                    footer:SetAlpha(1)
                elseif forced then
                    forced = false
                    footer:SetAlpha(Lobby.EBDDFIIHID() and 1 or 0)
                end
            end
            footer:addEventHandler("update_lobby_status", update)
            menu:addEventHandler("searching_for_lobby", update)
            update()
        end)
    end
end

-- Native UIRoot owns the chat footer, input, welcome line and visibility. Enable
-- its creation only during frontend root initialization; respect profile chat
-- preferences and leave the gameplay root/transport untouched.
if LUI.UIRoot and LUI.TextChat and type(LUI.UIRoot.init) == "function" then
    local rootInit, nativeBool = LUI.UIRoot.init, Dvar.IBEGCHEFE
    local constructing = false
    Dvar.IBEGCHEFE = function(name, ...)
        if constructing and (name == "LKSKPKTOON" or name == "NQOMTKMNPT") then return true end
        return nativeBool(name, ...)
    end
    local function finish(previous, ok, ...)
        constructing = previous
        if not ok then error((...), 0) end
        return ...
    end
    local function scoped(callback, ...)
        local previous = constructing
        constructing = true
        return finish(previous, pcall(callback, ...))
    end
    LUI.UIRoot.init = function(...) return scoped(rootInit, ...) end
    -- The layer checks support again every frame. Enabling only construction
    -- creates an invisible footer. Keep native fence/profile/menu visibility.
    -- Platform availability now comes from the native service-backed binding.
    -- Keep only this frontend layer's shipped chat-support dvar scope.
    for _, name in ipairs({"ShouldBeVisible", "InitLayer", "Frame"}) do
        local callback = LUI.TextChat[name]
        if type(callback) == "function" then
            LUI.TextChat[name] = function(...) return scoped(callback, ...) end
        end
    end
    wrap("mptabsmenu", function(menu, controller)
        menu:Wait(1).onComplete = function()
            local root = Engine.DBFFAEEFGJ()
            if root and not root.textChat then
                root.textChat = root:AddLayer(LUI.TextChat.new({controllerIndex = controller}))
            end
        end
    end)
end

-- Host campaigns are consumed by the original CRM carousel and popup code.
-- Retail CRMMain captures its rows at construction. Refresh those rows before
-- its existing update handler runs, including late replies and empty catalogs.
do
    local build = MenuBuilder.BuildRegisteredType
    MenuBuilder.BuildRegisteredType = function(name, ...)
        -- CRM is loaded lazily on some boot paths, after this startup module.
        if string.lower(name) ~= "crmmain" then return build(name, ...) end
        local add = LUI.UIElement.addEventHandler
        local connected = false
        LUI.UIElement.addEventHandler = function(widget, event, handler, ...)
            if event == "update_crm_data" then
                local native = handler
                handler = function(self, event)
                    self._menuMessageData = CRM.GetMenuMessageData()
                    self._patchNoteData = CRM.GetPatchNoteMessageData()
                    local index = self._currentMessageIndexDatasource
                    if index and (index:GetValue(self._controllerIndex) or 0) >= #self._menuMessageData then
                        index:SetValue(self._controllerIndex, 0)
                    end
                    return native(self, event)
                end
                connected = true
            end
            return add(widget, event, handler, ...)
        end
        local ok, widget = pcall(build, name, ...)
        LUI.UIElement.addEventHandler = add
        if not ok then error(widget, 0) end
        ReplayOnline.Log("native CRM campaign refresh connected=" .. tostring(connected))
        return widget
    end
end
-- BEGIN TU19 PRIVATE START
do
    local native = MenuBuilder.m_types.privatematchlobbybuttons
    if type(native) == 'function' and rawget(MenuBuilder, '_replayPrivateStart') ~= native then
        local function pack(...) return {n=select('#',...),...} end
        local function applies(menu)
            return Engine.DDJFBBJAIG() and
                (CONDITIONS.IsCoreMultiplayer(menu) or CONDITIONS.IsMagmaGameMode(menu))
                and IsPrivateMatch() and not IsSystemLink()
        end
        local wrapped = function(...)
            local results = pack(native(...))
            local menu = results[1]
            for _, name in ipairs({'StartMatch', 'StartMatchDS'}) do
                local button = menu and menu[name]
                local previous = button and button.m_eventHandlers and button.m_eventHandlers.button_action
                if button and type(previous) == 'function' then
                    button:registerEventHandler('button_action', function(self, event)
                        if applies(menu) then
                            -- Use the same native command binding as TU19's
                            -- listen-server action, directly as requested.
                            -- Host/map availability still uses the stock
                            -- button's model subscriptions and disabled state.
                            ReplayOnline.Log('private Start Match: xpartygo')
                            Engine.EBIDFIDHIE('xpartygo')
                            return true
                        end
                        return previous(self, event)
                    end)
                end
            end
            return unpack(results, 1, results.n)
        end
        MenuBuilder.m_types.privatematchlobbybuttons = wrapped
        rawset(MenuBuilder, '_replayPrivateStart', wrapped)
    end
end
-- END TU19 PRIVATE START
ReplayOnline.Log("native frontend service adapters installed: matchmaking, CRM; chat=" .. tostring(LUI.TextChat ~= nil))
