-- TU19 native widgets and existing ReplayOnline exports; no later-build assets.
if not ReplayOnline.Enabled() or type(ReplayOnline.ClientSettingsStatus) ~= "function" then return end
if rawget(_G, "ReplayClientSettings") then return end
local settings = {builders = setmetatable({}, {__mode = "k"})}
rawset(_G, "ReplayClientSettings", settings)
local function pack(...) return {n = select("#", ...), ...} end
local function label(menu, id, text, y, x, width)
    local item = LUI.UIText.new(); item.id = id
    x = x or 90; width = width or 1210
    item:SetAnchorsAndPosition(0, 1, 0, 1, x * _1080p, (x + width) * _1080p, y * _1080p, (y + 30) * _1080p)
    item:SetFont(FONTS.GetFont(FONTS.MainRegular.File)); item:SetAlignment(LUI.Alignment.Left)
    item:setText(text); menu:addElement(item); return item
end
local unlockMessages = {
    disconnected = "Backend disconnected. Unlock state will refresh when it reconnects.",
    connecting = "Reading your saved unlock state...",
    pending = "Saving unlock state...",
    saved = "Unlock state saved to your account.",
    conflict = "The unlock state changed elsewhere. Review it and try again.",
    storage_error = "The server could not save this change. Please try again."
}
function settings.Snapshot()
    local ready, pending, enabled, message, revision = ReplayOnline.ClientSettingsStatus()
    local noclipAvailable, noclipEnabled = ReplayOnline.NoclipState()
    return {ready = ready, pending = pending, enabled = enabled, message = message,
        revision = revision, noclipAvailable = noclipAvailable, noclipEnabled = noclipEnabled}
end
function settings.Build(parent, options)
    local controller = options and options.controllerIndex or 0
    local menu = LUI.UIElement.new(); menu.id = "ReplayClientSettings"
    menu:SetAnchorsAndPosition(0, 0, 0, 0, 0, 0, 0, 0)
    -- Barracks places WorldBlur and a half-opacity dark layer behind its
    -- content. Use those native widgets here in both the lobby and match VM.
    local blur
    if type(MenuBuilder.m_types.WorldBlur) == "function" then
        blur = MenuBuilder.BuildRegisteredType("WorldBlur", {controllerIndex = controller})
    else
        blur = LUI.UIElement.new()
        blur:SetBlurStrength(5)
        blur:setupWorldBlur()
    end
    blur.id = "WorldBlur"
    blur:SetAnchorsAndPosition(0, 0, 0, 0, 2 * _1080p, 0, 0, 0)
    menu:addElement(blur)
    local darken = LUI.UIImage.new(); darken.id = "Darken"
    darken:SetRGBFromTable(SWATCHES.genericMenu.blackBackground, 0)
    darken:SetAlpha(0.5, 0)
    menu:addElement(darken)
    local title = MenuBuilder.BuildRegisteredType("MenuTitle", {controllerIndex = controller})
    title:SetTitle("CLIENT SETTINGS"); menu:addElement(title)
    if Engine.DDJFBBJAIG() and type(ReplayMenu1645) == "table" and
        type(CONDITIONS.IsWZWipDvarEnabled) == "function" and CONDITIONS.IsWZWipDvarEnabled() then
        -- The later frontend reserves the first 180 pixels for its header.
        -- Keep the title within this tab's content, leaving the pause menu as is.
        title:SetAnchorsAndPosition(0, 1, 0, 1, 0, 1024 * _1080p, 175 * _1080p, 280 * _1080p)
    end
    label(menu, "InventoryHeading", "INVENTORY", 325)
    local status = label(menu, "UnlockStatus", "Reading your saved unlock state...", 255, 730, 1000)
    label(menu, "EarnedState", "Re-Lock All preserves earned items, purchases and progression.", 290, 730, 1000)
    local feedback = label(menu, "SettingsFeedback", "", 425, 730, 1000)
    label(menu, "MovementHeading", "MOVEMENT", 575)
    local movement = label(menu, "MovementStatus", "", 475, 730, 1000)
    label(menu, "NoclipShortcut", "Right Ctrl + N  |  Console: noclip", 520, 730, 1000)
    if type(ReplayOnline.CamoStatus) == "function" then
        label(menu, "CustomizationHeading", "CUSTOMIZATION", 325, 920, 790)
    end
    local localFeedback, lastRevision
    local function button(id, text, y, action)
        local b = MenuBuilder.BuildRegisteredType("MenuButton", {controllerIndex = controller})
        b.id = id; b.Text:setText(text)
        b:SetAnchorsAndPosition(0, 1, 0, 1, 90 * _1080p, 650 * _1080p, y * _1080p, (y + 44) * _1080p)
        b:registerEventHandler("button_action", function()
            if controller == 0 and b._replayAvailable and action() then
                localFeedback = (id == "UnlockAll" or id == "RelockAll") and
                    "Request queued. Waiting for the server to save it." or "Request accepted."
            else localFeedback = "Unavailable. Check the current state and try again." end
            feedback:setText(localFeedback)
            return true
        end)
        menu:addElement(b); return b
    end
    local unlock = button("UnlockAll", "Unlock All", 370, function() return ReplayOnline.SetUnlockAll(1) end)
    local relock = button("RelockAll", "Re-Lock All", 430, function() return ReplayOnline.SetUnlockAll(0) end)
    local noclip = button("Noclip", "Noclip", 610, function()
        local available, active = ReplayOnline.NoclipState()
        return (available or active) and ReplayOnline.SetNoclip(active and 0 or 1)
    end)
    if type(ReplayOnline.CamoStatus)=='function' then
        local camos=button('CamoChanger','In-Match Camo Changer',370,function()
            LUI.FlowManager.RequestAddMenu('ReplayCamoChanger',true,controller,false,{controllerIndex=controller});return true
        end)
        camos:SetAnchorsAndPosition(0,1,0,1,920*_1080p,1710*_1080p,370*_1080p,414*_1080p)
        camos._replayAvailable=true
    end
    if type(ReplayOnline.ProfileStatus)=='function' then
        local profile=button('ProfileSync','Profile Sync / Recover Saved Loadouts',805,function()
            LUI.FlowManager.RequestAddMenu('ReplayProfileSync',true,controller,false,{controllerIndex=controller});return true
        end)
        profile._replayAvailable=true
    end
    if type(ReplayOnline.Tournament)=='function' then
        local tournament=button('Tournament','Gunfight Tournament',925,function()
            ReplayOnline.Tournament('command','refresh')
            LUI.FlowManager.RequestAddMenu('ReplayTournament',true,controller,false,{controllerIndex=controller});return true
        end)
        tournament._replayAvailable=true
    end
    local function enabled(b, value) b._replayAvailable = value; b:SetButtonDisabled(not value) end
    local tickets
    if type(ReplayOnline.TrialTickets)=='function' then
        tickets=button('FreeTrialTickets','Get 5 Trial Tickets (Free)',865,function()return ReplayOnline.GrantTrialTickets()end)
    end
    local function refresh()
        local snapshot = settings.Snapshot()
        if lastRevision and snapshot.revision ~= lastRevision then localFeedback = nil end
        if snapshot.message == "conflict" or snapshot.message == "disconnected" or
            snapshot.message == "storage_error" then localFeedback = nil end
        lastRevision = snapshot.revision
        enabled(unlock, controller == 0 and snapshot.ready and not snapshot.pending and not snapshot.enabled)
        enabled(relock, controller == 0 and snapshot.ready and not snapshot.pending and snapshot.enabled)
        status:setText(snapshot.ready and ("Unlock All: " .. (snapshot.enabled and "ENABLED" or "DISABLED")) or
            "Unlock All: checking saved state...")
        feedback:setText(snapshot.pending and unlockMessages.pending or localFeedback or
            unlockMessages[snapshot.message] or "")
        enabled(noclip, controller == 0 and (snapshot.noclipAvailable or snapshot.noclipEnabled))
        noclip.Text:setText("Noclip: " .. (snapshot.noclipEnabled and "ON" or "OFF"))
        movement:setText(snapshot.noclipAvailable and "Available in this locally hosted match." or
            "Spawn in a locally hosted private match to use noclip.")
        if tickets then
            local online,saving,count=ReplayOnline.TrialTickets();enabled(tickets,controller == 0 and online and not saving)
            tickets.Text:setText('Get 5 Trial Tickets (Free)  |  Owned: '..tostring(count or 0))
        end
    end
    menu:addElement(LUI.UITimer.new({interval = 500, event = "replay_settings_refresh"}))
    menu:addAndCallEventHandler("replay_settings_refresh", refresh)
    local bind = LUI.UIBindButton.new(); menu:addElement(bind)
    bind:registerEventHandler("button_secondary", function() LUI.FlowManager.RequestLeaveMenu(menu); return true end)
    return menu
end
MenuBuilder.registerType("ReplayClientSettings", settings.Build)
if type(ReplayOnline.CamoStatus)=='function' then
    MenuBuilder.registerType('ReplayCamoChanger',function(parent,options)
        local controller=options and options.controllerIndex or 0
        local menu=LUI.UIElement.new();menu.id='ReplayCamoChanger'
        menu:SetAnchorsAndPosition(0,0,0,0,0,0,0,0)
        local title=MenuBuilder.BuildRegisteredType('MenuTitle',{controllerIndex=controller});title:SetTitle('IN-MATCH CAMOS');menu:addElement(title)
        local status=label(menu,'CamoStatus','Reading your equipped weapon...',205)
        local feedback=label(menu,'CamoFeedback','Changes apply to your current weapon. Ammo and loadouts are preserved.',250)
        local pageLabel=label(menu,'CamoPage','',855)
        local rows,page,buttons={},1,{}
        local function button(id,text,x,y,action)
            local b=MenuBuilder.BuildRegisteredType('MenuButton',{controllerIndex=controller});b.id=id;b.Text:setText(text)
            b:SetAnchorsAndPosition(0,1,0,1,x*_1080p,(x+670)*_1080p,y*_1080p,(y+42)*_1080p)
            b:registerEventHandler('button_action',function()if not b.disabled then action(b)end;return true end)
            menu:addElement(b);return b
        end
        for i=1,10 do
            buttons[i]=button('Camo'..i,'',90,275+i*50,function(b)
                if b.owned and b.ref then
                    feedback:setText(ReplayOnline.CamoCommand('apply',b.ref) and 'Camo request sent.' or 'Weapon changed or account unavailable. Refresh and retry.')
                end
            end)
        end
        local function refresh()
            local message,catalog=ReplayOnline.CamoStatus();status:setText(message or '')
            rows={}
            for ref,owned in string.gmatch(catalog or '','([a-z0-9_]+)|([01])\n')do rows[#rows+1]={ref=ref,owned=owned=='1'}end
            local pages=math.max(1,math.ceil(#rows/10));page=math.min(page,pages)
            pageLabel:setText('Page '..page..' / '..pages)
            for i,b in ipairs(buttons)do
                local row=rows[(page-1)*10+i];b.ref=row and row.ref;b.owned=row and row.owned or false
                b.disabled=not b.owned;b:SetButtonDisabled(b.disabled)
                b.Text:setText(row and row.ref..(row.owned and '' or '  (Locked)') or '')
            end
        end
        button('PreviousCamos','Previous Page',90,920,function()page=math.max(1,page-1);refresh()end)
        button('NextCamos','Next Page',850,920,function()page=math.min(math.max(1,math.ceil(#rows/10)),page+1);refresh()end)
        button('RestoreCamo','Restore Original Camo',850,375,function()
            feedback:setText(ReplayOnline.CamoCommand('restore','') and 'Restore request sent.' or 'Refresh your equipped weapon first.')
        end)
        button('RefreshCamo','Refresh Equipped Weapon',850,325,function()ReplayOnline.CamoCommand('inspect','');refresh()end)
        ReplayOnline.CamoCommand('inspect','')
        menu:addElement(LUI.UITimer.new({interval=500,event='replay_camo_refresh'}))
        menu:addAndCallEventHandler('replay_camo_refresh',refresh)
        local bind=LUI.UIBindButton.new();menu:addElement(bind)
        bind:registerEventHandler('button_secondary',function()LUI.FlowManager.RequestLeaveMenu(menu);return true end)
        return menu
    end)
end
if type(ReplayOnline.ProfileStatus)=='function' then
    MenuBuilder.registerType('ReplayProfileSync',function(parent,options)
        local controller=options and options.controllerIndex or 0
        local menu=LUI.UIElement.new();menu.id='ReplayProfileSync'
        menu:SetAnchorsAndPosition(0,0,0,0,0,0,0,0)
        local title=MenuBuilder.BuildRegisteredType('MenuTitle',{controllerIndex=controller});title:SetTitle('PROFILE SYNC');menu:addElement(title)
        local status=label(menu,'ProfileStatus','',245)
        label(menu,'RecoveryCopies','Recovery keeps a backup of both copies in your players folder.',300)
        label(menu,'RecoveryScope','This choice affects profile data and loadouts. Earned progression stays on the server.',340)
        local choices={}
        for index,text in ipairs({'Use server profile and loadouts','Keep this PC\'s profile and loadouts'})do
            local choice=index-1
            local button=MenuBuilder.BuildRegisteredType('MenuButton',{controllerIndex=controller})
            button.id=choice==0 and 'UseServerProfile' or 'UseLocalProfile';button.Text:setText(text)
            button:SetAnchorsAndPosition(0,1,0,1,90*_1080p,850*_1080p,(410+index*65)*_1080p,(454+index*65)*_1080p)
            button:registerEventHandler('button_action',function()
                if button._available and ReplayOnline.ResolveProfile(choice) then status:setText('Resolving saved profile...')end
                return true
            end)
            menu:addElement(button);choices[#choices+1]=button
        end
        menu:addElement(LUI.UITimer.new({interval=500,event='replay_profile_refresh'}))
        menu:addAndCallEventHandler('replay_profile_refresh',function()
            local ready,pending,conflict,reason=ReplayOnline.ProfileStatus()
            status:setText(conflict and 'Two saved copies differ. Choose which one to use.' or pending and 'Syncing your profile...' or ready and 'Profile saved on the server.' or 'Profile status: '..tostring(reason))
            for _,button in ipairs(choices)do
                button._available=conflict and not pending and not (reason=='storage_error' and button.id=='UseLocalProfile')
                button:SetButtonDisabled(not button._available)
            end
        end)
        if type(ReplayOnline.PreferencesStatus)=='function' then
            local preferences=label(menu,'PreferencesStatus','',660)
            label(menu,'PreferencesScope','Controls, sensitivity, audio and other preferences sync separately.',705)
            local preferenceChoices={}
            for index,text in ipairs({'Use server preferences','Keep this PC\'s preferences'})do
                local choice=index-1
                local button=MenuBuilder.BuildRegisteredType('MenuButton',{controllerIndex=controller})
                button.id=choice==0 and 'UseServerPreferences' or 'UseLocalPreferences';button.Text:setText(text)
                button:SetAnchorsAndPosition(0,1,0,1,90*_1080p,850*_1080p,(720+index*65)*_1080p,(764+index*65)*_1080p)
                button:registerEventHandler('button_action',function()
                    if button._available then ReplayOnline.ResolvePreferences(choice)end
                    return true
                end)
                menu:addElement(button);preferenceChoices[#preferenceChoices+1]=button
            end
            menu:addElement(LUI.UITimer.new({interval=500,event='replay_preferences_refresh'}))
            menu:addAndCallEventHandler('replay_preferences_refresh',function()
                local state=ReplayOnline.PreferencesStatus()
                preferences:setText(state=='conflict' and 'Preferences differ. Choose which copy to use.' or 'Preferences: '..tostring(state))
                for _,button in ipairs(preferenceChoices)do
                    button._available=state=='conflict';button:SetButtonDisabled(not button._available)
                end
            end)
        end
        local bind=LUI.UIBindButton.new();menu:addElement(bind)
        bind:registerEventHandler('button_secondary',function()LUI.FlowManager.RequestLeaveMenu(menu);return true end)
        return menu
    end)
end

-- Preserve the stock tab builder and tab-manager focus/navigation. TU19's
-- SetupTabs consumes isPlayTab/menuName descriptors just as the original UI.
local constructor = MenuBuilder.m_types.mptabsmenu
if type(constructor) == "function" then
    MenuBuilder.m_types.mptabsmenu = function(parent, options)
        local menu = constructor(parent, options)
        local setup = menu.SetupTabs
        if type(setup) ~= "function" then return menu end
        if not settings.builders[setup] then
            local env = getfenv(setup); local nativeIpairs = env.ipairs or ipairs
            setfenv(setup, setmetatable({ipairs = function(rows)
                -- Replay's MPTabsMenu starts with menuName=MPPlayMenu (or BRPlayMenu).
                -- Its retail row has no isPlayTab field; identify the actual
                -- first tab before the native ipairs builds tab buttons.
                local first = type(rows) == "table" and rows[1]
                if first and (first.menuName == "MPPlayMenu" or first.menuName == "BRPlayMenu" or
                    first.menuName == "CPPlayMenu" or first.menuName == "LobbyFlowWidget") then
                    local found = false
                    for _, row in nativeIpairs(rows) do if row.menuName == "ReplayClientSettings" then found = true end end
                    if not found then rows[#rows + 1] = {name = "CLIENT SETTINGS", menuName = "ReplayClientSettings", canClearNewIndicators = false} end
                end
                return nativeIpairs(rows)
            end}, {__index = env, __newindex = env}))
            settings.builders[setup] = true
        end
        menu.SetupTabs = function(self, ...)
            local result = pack(setup(self, ...))
            for index, row in ipairs(self._tabsData or {}) do
                if row.menuName == "ReplayClientSettings" and self._tabIndexMap then self._tabIndexMap.ReplayClientSettings = index end
            end
            return unpack(result, 1, result.n)
        end
        return menu
    end
end
for _, name in ipairs({"optionsmenu", "optionsmenunewlayout"}) do
    local build = MenuBuilder.m_types[name]
    if type(build) == "function" then
        MenuBuilder.m_types[name] = function(parent, options)
            local menu = build(parent, options)
            local controller = options and options.controllerIndex or 0
            local button = MenuBuilder.BuildRegisteredType("MenuButton", {controllerIndex = controller})
            button.id = "ReplayClientSettingsEntry"; button.Text:setText("CLIENT SETTINGS")
            button:SetAnchorsAndPosition(0, 1, 0, 1, 90 * _1080p, 650 * _1080p, 880 * _1080p, 924 * _1080p)
            button:registerEventHandler("button_action", function()
                LUI.FlowManager.RequestAddMenu("ReplayClientSettings", true, controller, false, {}, true); return true
            end)
            menu:addElement(button); return menu
        end
    end
end

-- Extend the native MP and BR pause button lists rather than replacing their
-- Loadouts, Return, End Game and Quit Desktop actions. BR keeps an explicit
-- focus list, so new buttons also receive native list indices.
local function addPauseButtons(list, options, isBR)
    if not list or list.ReplayClientSettingsButton then return list end
    local controller = options and options.controllerIndex or list:getRootController()
    local firstY = isBR and 420 or 480
    if not list.QuitDesktopButton then firstY = firstY - 60 end
    local function add(id, caption, y, action)
        local button = MenuBuilder.BuildRegisteredType("GenericButton", {controllerIndex = controller})
        button.id = id
        button.Text:SetLeft(20 * _1080p, 0)
        button.Text:setText(caption, 0)
        button.Text:SetAlignment(LUI.Alignment.Left)
        button:SetAnchorsAndPosition(0, 1, 0, 1, 0, 500 * _1080p,
            y * _1080p, (y + 48) * _1080p)
        button:addEventHandler("button_action", function(_, event)
            action(event and event.controller or controller)
            return true
        end)
        list:addElement(button)
        list[id] = button
        if isBR and list._buttonList then
            table.insert(list._buttonList, button)
            button.listIndex = #list._buttonList
        end
    end
    add("ReplayClientSettingsButton", "CLIENT SETTINGS", firstY, function(index)
        LUI.FlowManager.RequestAddMenu("ReplayClientSettings", true, index, false,
            {controllerIndex = index}, true)
    end)
    add("ReplayDisconnectButton", "DISCONNECT", firstY + 60, function()
        ReplayOnline.Disconnect()
    end)
    list:SetAnchorsAndPosition(0, 1, 0, 1, 0, 500 * _1080p,
        0, (firstY + 108) * _1080p)
    return list
end

for _, entry in ipairs({{"pausemenubuttons", false}, {"brpausemenubuttons", true}}) do
    local name, isBR = entry[1], entry[2]
    local build = MenuBuilder.m_types[name]
    if type(build) == "function" and type(ReplayOnline.Disconnect) == "function" then
        MenuBuilder.m_types[name] = function(parent, options)
            return addPauseButtons(build(parent, options), options, isBR)
        end
    end
end
for _, entry in ipairs({{"mppausemenu", 155, 743}, {"brpausemenu", 238, 766}}) do
    local name, top, bottom = entry[1], entry[2], entry[3]
    local build = MenuBuilder.m_types[name]
    if type(build) == "function" then
        MenuBuilder.m_types[name] = function(parent, options)
            local menu = build(parent, options)
            if menu.PauseMenuButtons and menu.PauseMenuButtons.ReplayDisconnectButton then
                menu.PauseMenuButtons:SetAnchorsAndPosition(0.5, 0.5, 0, 1,
                    -830 * _1080p, -330 * _1080p, top * _1080p, bottom * _1080p)
            end
            return menu
        end
    end
end

-- Backend-owned bracket, using TU19's existing native widgets and join path.
if type(ReplayOnline.Tournament)=='function' then
    local function tournamentMenu(parent,options)
        local controller=options and options.controllerIndex or 0
        local menu=LUI.UIElement.new();menu.id='ReplayTournament'
        menu:SetAnchorsAndPosition(0,0,0,0,0,0,0,0)
        local title=MenuBuilder.BuildRegisteredType('MenuTitle',{controllerIndex=controller});title:SetTitle('GUNFIGHT TOURNAMENT');menu:addElement(title)
        local info=label(menu,'TournamentStatus','Connecting...',210)
        label(menu,'TournamentRules','16 teams of two. Register solo or with one friend. Every player must ready up.',260)
        label(menu,'TournamentRewards','Win rounds to earn the original tournament items and XP.',305)
        local feedback=label(menu,'TournamentFeedback','',880)
        local function get(key) return ReplayOnline.Tournament('get',key) or '' end
        local actions={}
        for index,entry in ipairs({{'register','Register'},{'ready','Ready for next match'},{'join','Join assigned match'},{'cancel','Withdraw'}})do
            local op,text=entry[1],entry[2]
            local b=MenuBuilder.BuildRegisteredType('MenuButton',{controllerIndex=controller});b.Text:setText(text);b.id='Tournament_'..op
            b:SetAnchorsAndPosition(0,1,0,1,90*_1080p,690*_1080p,(325+index*65)*_1080p,(369+index*65)*_1080p)
            b:registerEventHandler('button_action',function()
                if b._available then
                    local ok
                    if op=='join' then
                        local category,child=ReplayOnline.Tournament('playlist')
                        ok=category and child and ReplayOnline.Tournament('join') and Playlist.CDBBECIGIG(tonumber(category),tonumber(child))
                        if ok then LUI.FlowManager.RequestAddMenu('MPPublicLobby',false,controller,false,{}) end
                    else ok=ReplayOnline.Tournament('command',op) end
                    feedback:setText(ok and 'Request accepted.' or 'Request unavailable. Check your party and connection.')
                end
                return true
            end)
            menu:addElement(b);actions[op]=b
        end
        local rows={};for i=0,3 do rows[i]=label(menu,'Bracket'..i,'',640+i*48)end
        menu:addElement(LUI.UITimer.new({interval=500,event='replay_tournament_refresh'}))
        menu:addAndCallEventHandler('replay_tournament_refresh',function()
            local online=get('online')=='1';local pending=get('pending')=='1'
            local state=tonumber(get('/status')) or 0;local registered=get('/registered')=='1'
            local round=tonumber(get('/pending_match')) or -1
            info:setText(not online and 'Connecting to tournament service...' or registered and ('Players registered: '..get('/queued_players')..' / 32') or
                state==3 and 'Tournament won!' or state==2 and 'Tournament complete.' or state==1 and ('Bracket active. Wins: '..get('/wins')) or 'Registration is open.')
            for op,b in pairs(actions)do
                b._available=online and not pending and controller==0 and
                    (op=='cancel' and (registered or state==1) or op=='register' and not registered and state~=1 or
                     op=='ready' and state==1 or op=='join' and get('/join')=='1')
                b:SetButtonDisabled(not b._available)
            end
            local id=get('/xuid');local isReady=false
            for i=0,31 do if get('/ready/'..i)==id and id~='' then isReady=true end end
            actions.ready.Text:setText(isReady and 'Ready - waiting for all four players' or 'Ready for next match')
            local base=round>=0 and '/bracket/matches/'..round..'/teams/' or ''
            for side=0,1 do
                local team=base~='' and get(base..side) or ''
                for player=0,1 do
                    local name=team~='' and get('/bracket/teams/'..team..'/'..player..'/name') or ''
                    rows[side*2+player]:setText(name~='' and ((side==0 and 'Team A: ' or 'Team B: ')..name) or '')
                end
            end
        end)
        local bind=LUI.UIBindButton.new();menu:addElement(bind)
        bind:registerEventHandler('button_secondary',function()LUI.FlowManager.RequestLeaveMenu(menu);return true end)
        return menu
    end
    MenuBuilder.registerType('ReplayTournament',tournamentMenu)
    -- Existing TU19 entry points use the same provider screen.
    for _,name in ipairs({'tournamentsignup','tournamentsignupalpha','tournamentloading','arenatournament'})do
        local original=MenuBuilder.m_types[name]
        if type(original)=='function' then
            MenuBuilder.m_types[name]=function(parent,options)
                if options and options.controllerIndex and options.controllerIndex~=0 then return original(parent,options)end
                return tournamentMenu(parent,options)
            end
        end
    end
end
