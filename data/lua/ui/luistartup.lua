-- MP-only port of the repository Eightx startup. Native TU19 Playlist exports
-- and datasources remain authoritative; local unlock/daily overrides below are
-- explicitly requested for this standalone build.
jit.off()
local callbacks = {}
function LUI_PostLoad(callback)
    callbacks[#callbacks + 1] = callback
end

local function load(name)
    Engine.BHFFIBEFFH(name, "requires")
    require(name)
    Engine.DCHDDCJCEG(name, "requires")
end

DebugPrint("[Replay] MP LUI startup begin")
ReplayOnline.Log("MP startup executing")
if ReplayOnline.Enabled() then
    -- TU19's local strings omit this playlist label. The native model builder
    -- localizes names in C++, so fix its model as well as direct Lua lookups.
    local soloNameKey = "PLAYLIST/BRSOLOSTIM_NAME2"
    if type(Engine.CBBHFCGDIC) == "function" then
        local localize = Engine.CBBHFCGDIC
        Engine.CBBHFCGDIC = function(key, ...)
            if key == soloNameKey then return "BR Buy Back Solos" end
            return localize(key, ...)
        end
    end
    if Lobby and type(Lobby.DFJICHHJFI) == "function" then
        local populatePlaylists = Lobby.DFJICHHJFI
        Lobby.DFJICHHJFI = function(path)
            populatePlaylists(path) -- native TU19 export returns no values
            local controller = Engine.IJEBHJIJF()
            local function source(suffix) return LUI.DataSourceInGlobalModel.new(path .. suffix) end
            local function fixName(suffix)
                local name = source(suffix)
                if name:GetValue(controller) == soloNameKey then
                    name:SetValue(controller, "BR Buy Back Solos")
                end
            end
            for category = 0, math.min(source(".count"):GetValue(controller) or 0, 26) - 1 do
                fixName(".name." .. category)
                local childPath = "." .. category .. ".playlist"
                for child = 0, math.min(source(childPath .. ".count"):GetValue(controller) or 0, 20) - 1 do
                    fixName(childPath .. ".name." .. child)
                end
            end
        end
    end
    -- Install before requires so consumers cache these callbacks, not the stock
    -- matchmaking implementation. Arguments are native category/child indexes.
    if Playlist and Playlist.CDBBECIGIG then
        local nativeAction = Playlist.CDBBECIGIG
        Playlist.CDBBECIGIG = function(category, child)
            if Lobby.GetLocalPlayerDataSource then
                local controller = Engine.IJEBHJIJF()
                local player = Lobby.GetLocalPlayerDataSource(controller)
                if player then
                    ReplayOnline.QueueCard(Engine.DDAFEGDJHC(controller) or "",
                        player.rank:GetValue(controller) or 0, player.prestige:GetValue(controller) or 0,
                        player.emblemIndex:GetValue(controller) or 0, player.backgroundIndex:GetValue(controller) or 0)
                end
            end
            if child == 200 then
                -- TU19 QuickPlayButton uses 200 for its enabled filter set,
                -- not the first entry. Query the same native filter flags as
                -- PlaylistUtils.ValidateSelection without rebuilding UI models.
                local selected = {}
                for index = 0, 19 do
                    if ReplayOnline.HasCategoryChild(category, index) then
                        local _, enabled = Playlist.CGJBAEBBG(category, index)
                        if enabled then selected[#selected + 1] = index end
                    end
                end
                if #selected == 0 or not ReplayOnline.Start(category, unpack(selected)) then return false end
            elseif not ReplayOnline.Start(category, child) then return false end
            -- Replay's action sets category/profile and queues xstartlobby,
            -- which establishes the native party/session used by join handling.
            local accepted = nativeAction(category, child)
            if not accepted then ReplayOnline.Cancel() end
            return accepted
        end
        ReplayOnline.Log("native playlist action bridged to shared matchmaking")
    else
        ReplayOnline.Log("missing native playlist action export")
    end
    if Lobby and Lobby.CCCCACICHH then
        Lobby.CCCCACICHH = ReplayOnline.IsSearching
        ReplayOnline.Log("native searching query bridged to backend state")
    end
    -- Observe the stock fence results. Online entry now depends on the local
    -- provider's authenticated responses; Lua never manufactures completion.
    local function traceFence(namespace, key, name)
        local original = namespace and namespace[key]
        if not original then
            ReplayOnline.Log("missing native fence export " .. name)
            return
        end
        local previous = {}
        namespace[key] = function(...)
            local state, err = original(...)
            local args = {...}
            local index = tostring(args[1]) .. ":" .. tostring(args[2])
            local value = tostring(state) .. ":" .. tostring(err)
            if previous[index] ~= value then
                previous[index] = value
                ReplayOnline.Log("fence " .. name .. " args=" .. index .. " value=" .. value)
            end
            return state, err
        end
    end
    traceFence(Fences, "CCFEHFAFH", "services")
    traceFence(Fences, "CIHFIEHIDE", "data")
    traceFence(Fences, "IBEEFACJG", "patch")
    traceFence(Commerce, "CIIJICHHF", "commerce")
    traceFence(Engine, "BAAABFICDA", "platform")
end
load("ui.requires.AlwaysLoaded")
if Engine.DDJFBBJAIG() then
    load("ui.requires.FrontEnd")
    load("ui.requires.frontEnd.MP")
    if ReplayOnline.Enabled() then
        -- Expose the shipped Local Play blade alongside the online blades.
        if CONDITIONS and type(CONDITIONS.IsDevelopmentBuildLocalPlayAllowed) == "function" then
            CONDITIONS.IsDevelopmentBuildLocalPlayAllowed = function() return true end
        end
        local setup_system_link = Lobby and Lobby.SetupSystemLinkDvars
        if type(setup_system_link) == "function" then
            Lobby.SetupSystemLinkDvars = function(...)
                ReplayOnline.SetLocalPlay(1)
                setup_system_link(...)
                Dvar.DHEGHJJJHI("LPNMMPKRL", true)
            end
            -- ChooseLocalPlay and native restore/navigation can reach these
            -- menus without OpenSystemLinkMenu. Configure before their fences
            -- and again after parent-menu rebuilding has reset route dvars.
            local local_menus = {MPSystemLinkMenu = true, MPSystemLinkLobby = true}
            local online_menus = {MainMenu = true, MainMenuOffline = true, MPMainMenu = true, BRMainMenu = true,
                MPPublicLobby = true, BRPublicLobby = true, PrivateMatchLobby = true}
            local function select_route(name)
                if local_menus[name] and not ReplayOnline.SilentJoin() then
                    Lobby.SetupSystemLinkDvars()
                elseif online_menus[name] then
                    ReplayOnline.SetLocalPlay(0)
                end
            end
            local request_add_menu = LUI.FlowManager.RequestAddMenu
            LUI.FlowManager.RequestAddMenu = function(name, ...)
                select_route(name)
                return request_add_menu(name, ...)
            end
            local request_restore_menu = LUI.FlowManager.RequestRestoreMenu
            LUI.FlowManager.RequestRestoreMenu = function(name, ...)
                select_route(name)
                return request_restore_menu(name, ...)
            end
            local add_menu = LUI.FlowManager.addMenu
            LUI.FlowManager.addMenu = function(manager, event, ...)
                if event then select_route(event.menu) end
                return add_menu(manager, event, ...)
            end
            for _, name in ipairs({"mpsystemlinkmenu", "mpsystemlinklobby"}) do
                local build = MenuBuilder.m_types[name]
                if type(build) == "function" then
                    MenuBuilder.m_types[name] = function(...)
                        if not ReplayOnline.SilentJoin() then Lobby.SetupSystemLinkDvars() end
                        return build(...)
                    end
                end
            end
        end
        local offline_data_fetched = Fences and Fences.BGCHCGICDB
        if type(offline_data_fetched) == "function" then
            Fences.BGCHCGICDB = function(controller, ...)
                -- Configured clients restore ONLINE=0, while the stock offline
                -- predicate checks OFFLINE=1. Verify the active bank's actual
                -- six contexts instead of bypassing profile readiness.
                if controller == 0 and ReplayOnline.LocalPlay() then
                    return ReplayOnline.PrepareLocalPlayerData(controller)
                end
                return offline_data_fetched(controller, ...)
            end
        end
        local offline_stats_reset = LUI.Fence and LUI.Fence.fenceTypes and LUI.Fence.fenceTypes.offlineStatsReset
        if offline_stats_reset and type(offline_stats_reset.UpdateState) == "function" then
            local update_stats_reset = offline_stats_reset.UpdateState
            offline_stats_reset.UpdateState = function(self, ...)
                -- Validate/reset the same active bank consumed by local loadouts;
                -- preserve the native corruption/version checks and their dialogs.
                self._statsSource = ReplayOnline.LocalPlay() and CoD.StatsSource.ONLINE or CoD.StatsSource.OFFLINE
                return update_stats_reset(self, ...)
            end
        end
        ReplayOnline.Log("native local games/system link enabled alongside online services")
    end
    if Dvar and type(Dvar.IBEGCHEFE) == "function" then
        -- Keep the stock profile fence's EULA acceptance/save path active even
        -- if a later config changes the dvar. Other profile checks still run.
        local boot_flow_bool = Dvar.IBEGCHEFE
        Dvar.IBEGCHEFE = function(name, ...)
            if name == "LKSQOLNKLP" or name == "lui_skip_boot_flow" then return true end
            return boot_flow_bool(name, ...)
        end
        ReplayOnline.Log("terms screen skipped through native profile boot flow (lui_skip_boot_flow)")
    end
    if ReplayOnline.Enabled() and CODACCOUNT and type(CODACCOUNT.IsEnabled) == "function" then
        -- FenceOnlineServices uses AccountRegistrationEnabled (which calls
        -- IsEnabled) before opening CODAccountFencePopup/summary/OTP menus.
        -- Keep this UI gate disabled if a later config rewrites the dvar;
        -- native service authentication and readiness still run normally.
        CODACCOUNT.IsEnabled = function() return false end
        ReplayOnline.Log("Activision account prompts disabled (enable_cod_account)")
    end
    -- The MP-only require set omits this shipped dependency of
    -- ChallengesSummaryItem. Register the real progress bar before menus build.
    if not MenuBuilder.IsTypeRegistered("MPChallengeProgressBar") then
        load("ui.widgets.MPChallengeProgressBar")
        if not MenuBuilder.IsTypeRegistered("MPChallengeProgressBar") then
            error("Native MPChallengeProgressBar module did not register its constructor")
        end
        ReplayOnline.Log("native MPChallengeProgressBar dependency registered")
    end
else
    -- Original Replay BR art lives in common_br_mp, which this custom map
    -- does not load. The map helper supplies pixel-identical image aliases
    -- before its HUD is built; keep the native constructors and layout.
    local nativeRegisterMaterial = RegisterMaterial
    RegisterMaterial = function(name, ...)
        if escapeHudImages[name] and ReplayOnline.UseEscapeHUDArt() then
            return nativeRegisterMaterial(escapeHudImages[name], ...)
        end
        return nativeRegisterMaterial(name, ...)
    end
    -- Escape's temporary empty-loot policy leaves RKMMNSQKO empty. Native
    -- BRLootUtils still needs the existing Replay item definitions to draw
    -- equipped weapons/armor. Resolve only this client read to the original
    -- default; do not change the server dvar, loot rows, or HUD constructors.
    local lootTableString = Dvar.DHEEJCCJBH
    Dvar.DHEEJCCJBH = function(name, ...)
        local value = lootTableString(name, ...)
        if name == "RKMMNSQKO" and value == "" and ReplayOnline.UseDefaultEscapeHUDTable() then
            return "mp/loot_item_defs.csv"
        end
        return value
    end
    load("ui.requires.InGame")
    load("ui.requires.ingame.MP")
end
for _, callback in ipairs(callbacks) do
    callback()
end
if ReplayOnline.Enabled() and Engine.DDJFBBJAIG() and MenuBuilder.m_types then
    local login = MenuBuilder.m_types.codaccountlogin
    if type(login) == "function" then
        MenuBuilder.m_types.codaccountlogin = function(menu, options)
            local localOptions = {}
            for key, value in pairs(options or {}) do localOptions[key] = value end
            -- Local login keeps the saved identity; it does not merge a retail
            -- anonymous account or require the retail restart/update popup.
            localOptions.isAnonymousAccount = false
            local widget = login(menu, localOptions)
            widget.MenuTitle:SetTitle("LOGIN TO LOCAL ACCOUNT")
            local function edited(text, event)
                text.isValid = not event.isDefault and type(event.text) == "string"
                widget.ButtonLogin.isDisabled = not
                    (widget.InputEmail:GetTextElement().isValid and widget.InputPassword:GetTextElement().isValid)
                widget.ErrorMessage:SetAlpha(0)
            end
            widget.InputEmail:SetTextEditCompleteCallbackFunc(edited)
            widget.InputPassword:SetTextEditCompleteCallbackFunc(edited)
            -- Keep the native button, pending popup and account completion
            -- events. Validation of these two local placeholder fields accepts
            -- any text; the authenticated backend must still answer the login.
            widget:registerEventHandler("menu_close", function()
                AccountRegistration.DEEHFICICB(widget._controllerIndex)
            end)
            return widget
        end
    end
end
if ReplayOnline.Enabled() and LUI.TextChat and LUI.UIRoot then
    local initChat, clearChat = LUI.TextChat.InitLayer, LUI.TextChat.ClearLayer
    LUI.TextChat.InitLayer = function(layer, ...)
        initChat(layer, ...)
        local chat = layer.chat
        local input = chat and chat.TextChatFooter and chat.TextChatFooter.InputTextButton
        -- TU19's native clan channel is used for backend-wide Global. Keep
        -- stock channel events, message filtering and remote-task completion.
        local channels = LUI.TextChat.ChatChannels
        local globalType = OnlineChatType and OnlineChatType.CLAN_CHAT
        if channels and globalType and channels[globalType] then
            channels[globalType].name = "Global:"
            if input and type(input.textEditCompleteCallbackFunc) == "function" and not input._replayGlobal then
                input._replayGlobal = true
                local complete = input.textEditCompleteCallbackFunc
                input.textEditCompleteCallbackFunc = function(canceled, text, forceClose)
                    local command, rest
                    if type(text) == "string" then
                        command, rest = text:match("^/([%a]+)%s+(.*)$")
                        if not command then command = text:match("^/([%a]+)$"); rest = "" end
                    end
                    if not canceled and (command == "g" or command == "global") then
                        local channel = channels[globalType]
                        -- Do not send the command into a different channel
                        -- while the authenticated Global subscription loads.
                        if not channel.id then return end
                        chat._activeChatID, chat._activeChatType = channel.id, globalType
                        input._commandsModeActive = false
                        local label = chat.TextChatFooter.CurrentChannel
                        label:setText(channel.name)
                        label:SetRGBFromTable(channel.color)
                        local left, _, right = GetTextDimensions(channel.name, FONTS.GetFont(FONTS.BattleNetRegular.File), 20 * _1080p)
                        local x = chat.TextChatFooter._currentChannelInitialLeft + right - left + 3
                        label:SetRight(x); input:SetLeft(x)
                        Dvar.BDEHAEGHAF("LQPKMLTOQM", channel.id.id)
                        Dvar.DFIJDJFIFD("MOOOOTKPTL", globalType)
                        text = rest
                    end
                    return complete(canceled, text, forceClose)
                end
            end
        end
        if input and type(input.TryStartEditing) == "function" then
            local startEditing = input.TryStartEditing
            input.TryStartEditing = function(self, event)
                local result = startEditing(self, event)
                ReplayOnline.Log("chat input opened: editing=" .. tostring(self._textEditingActive) ..
                    " visible=" .. tostring(layer:ShouldBeVisible()) .. " inactive=" .. tostring(chat._chatInactive))
                return result
            end
        end
    end
    LUI.TextChat.ClearLayer = function(layer, ...)
        local chat = layer.chat
        local input = chat and chat.TextChatFooter and chat.TextChatFooter.InputTextButton
        -- Retail closes this tree without ending its edit session. A service
        -- disconnect can remove the layer while it owns native keycatch0x200.
        -- Use the stock editor's close callback before its elements are freed.
        if input and input._textEditingActive then
            input.TextEditOff()
            ReplayOnline.Log("chat input released before layer teardown")
        end
        if chat and chat._chatMouseCursorActive then
            chat._chatMouseCursorActive = false
            LUI.UIRoot.SetLuiLayerKeycatch(chat, chat._controllerIndex, false)
        end
        return clearChat(layer, ...)
    end
end
if ReplayOnline.Enabled() and not Engine.DDJFBBJAIG() and LUI.BRHUD then
    local initBRHUD = LUI.BRHUD.init
    LUI.BRHUD.init = function(self, controller)
        initBRHUD(self, controller)
        -- TU19 excludes this widget in Blood Money even though br.gsc sends
        -- ui_br_display_perk_info when it grants the selected class. Keep its
        -- native constructor/subscriptions: they show the actual equipped guns,
        -- equipment and perks, then dismiss the panel using stock animations.
        if CONDITIONS.IsBloodMoney(self) and not self.BRLoadoutFlyOut then
            self.BRLoadoutFlyOut = self:AddWidget("BRLoadoutFlyOut", {
                rightAnchor = 0, bottomAnchor = 1, topAnchor = 0, leftAnchor = 1,
                scalable = true, shakeable = false,
                horizontalOffset = _1080p * 96, verticalOffset = _1080p * 111,
                width = _1080p * 322, height = _1080p * 600
            })
            -- Native BRHUD.GetToggleWidgets already includes this field for
            -- gameplay, spectating and hidden-HUD states. Refresh once after
            -- registering the missing wrapper; no per-frame adapter is needed.
            self:UpdateWidgetsVisibility()
            ReplayOnline.Log("native Blood Money loadout summary enabled")
        end
    end
end
-- Replace only the backend-dependent rank readers. Native rank icons, thresholds
-- and progress widgets still use TU19's tables; the values come from saved awards.
if ReplayOnline.Enabled() and type(ReplayOnline.Progression) == "function" then
    local function earned(controller, season)
        return ReplayOnline.Progression(controller or 0, season or SEASON.GetCurrentSeason())
    end
    local bindings = {
        {"CBJHJGIGDA", 1}, -- military XP
        {"BDHJABHDDC", 2}, -- military rank (zero based)
        {"DFDGDCIAHI", 3}, -- officer XP
        {"CFEDCBACGI", 4} -- native officer rank getter is zero based
    }
    for _, binding in ipairs(bindings) do
        local name, index = binding[1], binding[2]
        local original = OnlineProgression[name]
        if type(original) == "function" then
            OnlineProgression[name] = function(...)
                local controller, season = ...
                local values = {earned(controller, season)}
                local value = tonumber(values[index])
                if value ~= nil then
                    return index == 4 and math.max(0, value - 1) or value
                end
                return original(...)
            end
        end
    end
    ReplayOnline.Log("rank display sourced from saved gameplay XP")
end
if ReplayOnline.Enabled() then
    -- Preserve native arguments/results when disabled. The process-owned switch
    -- survives frontend/gameplay VM reloads and defaults off on a clean launch.
    local returnTrueStatements = {
        {"PROGRESSION", "ShouldUnlockAll"}, {"PROGRESSION", "IsUnlocked"},
        {"PROGRESSION", "AreCustomLoadoutsUnlocked"}, {"OPERATOR", "IsUnlocked"},
        {"LOOT", "IsUnlocked"}, {"LOOT", "IsUnlockedWithID"},
        {"ATTACHMENT", "IsUnlocked"}, {"Unlocks", "BFCEGDJIEI"},
        {"LOOT", "IsItemIDEquippableByCurrentTitle"}, {"OnlineProgression", "CFEEHFADBE"}
    }
    local installed = 0
    for _, entry in ipairs(returnTrueStatements) do
        local tableRef = rawget(_G, entry[1])
        local original = tableRef and tableRef[entry[2]]
        if type(original) == "function" then
            tableRef[entry[2]] = function(...)
                if ReplayOnline.UnlockAll() then return true end
                return original(...)
            end
            installed = installed + 1
        else
            ReplayOnline.Log("unlock override unavailable: " .. entry[1] .. "." .. entry[2])
        end
    end
    ReplayOnline.Log("toggleable unlock checks installed: " .. installed .. "/" .. #returnTrueStatements)

    -- TU19's options builders cache a value getter when each tab is rebuilt.
    -- Use the live archived FOV for this row, including the in-game options VM.
    -- The reset action still uses the native default (80), not the current value.
    if OPTIONS_DATA and OPTIONS_DATA.optionsDataList and OPTIONS_DATA.optionsDataList.FOV then
        OPTIONS_DATA.optionsDataList.FOV.value = function()
            return Dvar.BJJCJHDBII("NNMSONQSOP")
        end
    end
    if Engine.DDJFBBJAIG() and Lobby and Lobby.GetModelDetailsFromPlayerCard then
        local getCardModels = Lobby.GetModelDetailsFromPlayerCard
        Lobby.GetModelDetailsFromPlayerCard = function(card, faction, controller, isLocalPlayer)
            -- Local standalone profiles do not upload/download a refreshed backend
            -- playercard. Read the selected operator from the real ranked DDL,
            -- just as TU19's single-player squad preview does. Remote cards retain
            -- their own models and the native streaming/character updater remains.
            if isLocalPlayer and controller == 0 then
                local selected = OPERATOR.GetSelectedOperatorIndex(controller)
                local body, head = OPERATOR.GetEquippedOperatorSkin(controller, selected)
                if body and body ~= "" and head and head ~= "" then return body, head end
            end
            return getCardModels(card, faction, controller, isLocalPlayer)
        end
        ReplayOnline.Log("local lobby operator models use selected profile skin")
    end

    local function installDailyAssignments(export, definition, mode)
        local selectedIDs
        ScheduledChallenges[export] = function()
            if not selectedIDs then
                local ids, seen = {}, {}
                -- The supplied source chooses three random later-version IDs.
                -- TU19 instead expects STRING keys matching its shipped CSV.
                -- Stable selection also survives frontend/ingame VM recreation
                -- without reseeding the game's global random-number generator.
                for index = 0, StringTable.BFHCAIIDA(definition.file) - 1 do
                    local row = CSV.ReadRow(definition, index)
                    local id = row.id and tostring(row.id)
                    local amount = tonumber(row.amount)
                    if id and tonumber(id) and tonumber(id) > 0 and not seen[id] and
                        amount and amount > 0 and row.name and row.name ~= "" and
                        row.desc and row.desc ~= "" then
                        ids[#ids + 1], seen[id] = id, true
                        if #ids == 3 then break end
                    end
                end
                -- Do not cache an empty result if the CSV has not loaded yet.
                if #ids == 0 then return {} end
                selectedIDs = ids
                ReplayOnline.Log("simulated " .. mode .. " daily assignments: " .. table.concat(ids, ","))
            end
            local assignments = {}
            for _, id in ipairs(selectedIDs) do assignments[id] = 0 end
            return assignments
        end
    end
    -- Native Challenge getters build the real reward/progress/localization
    -- records from these assignments. This is static simulated progress only;
    -- it does not award XP, write stats, or pretend to implement backend tracking.
    installDailyAssignments("DCIDHEEHJA", CSV.dailyChallenges, "MP")
    installDailyAssignments("JCAAACBBI", CSV.brDailyChallenges, "BR")
end
if Engine.DDJFBBJAIG() then
    -- Native arena initialization follows Lua startup. The main-thread startup
    -- pump scans it once; this local selector only reads that cached inventory.
    -- Private Match's selector can retain onlinegame=1/systemlink=0. Scope by
    -- the actual stock Maps builder, rather than by either network-mode flag.
    local buildingMapSelector = false
    local shouldDisplayMap = PRIVATE_MATCH.ShouldDisplayMap
    PRIVATE_MATCH.ShouldDisplayMap = function(index, controller, ...)
        if buildingMapSelector and not ReplayOnline.MapInstalled(Lobby.CJEHDHDGBH(index)) then
            return false
        end
        return shouldDisplayMap(index, controller, ...)
    end
    local buildMaps = MenuBuilder.m_types.maps
    MenuBuilder.m_types.maps = function(...)
        local previous = buildingMapSelector
        buildingMapSelector = true
        local ok, menu = pcall(buildMaps, ...)
        buildingMapSelector = previous
        if not ok then error(menu, 0) end
        return menu
    end
    local openKeyboard = OSK.CBJGJDGFC
    OSK.CBJGJDGFC = function(controller, title, value, limit, validateChars, checkProfanity, allowEmpty, callback, ...)
        local kind,capacity
        if title==Engine.CBBHFCGDIC('MENU/RENAME') then kind=0;capacity=20
        elseif title==Engine.CBBHFCGDIC('LUA_MENU/CUSTOM_CLAN_TAG') then kind=1;capacity=5
        elseif title==Engine.CBBHFCGDIC('CAS/ENTER_NAME') then kind=2;capacity=17 end
        if kind~=nil and type(callback)=='function' and type(ReplayOnline.CustomizationNameValid)=='function' then
            -- TU19 1ACBCE0 -> 1B9BA80 -> 1B9B890: arguments 5/6 select
            -- character/profanity checks. Keep all unrelated keyboard users.
            limit=capacity;validateChars=false;checkProfanity=false
            local nativeCallback=callback
            callback=function(index,text,...)
                if text==nil then return nativeCallback(index,text,...)end
                if ReplayOnline.CustomizationNameValid(text,kind) then
                    if kind==0 then text=text:gsub('%*([0-9])','^%1')end
                    return nativeCallback(index,text,...)
                end
                LUI.FlowManager.RequestPopupMenu(nil,'PopupOK',true,controller,false,{
                    title='NAME TOO LONG OR INVALID',message='Use a single-line name of at most '..tostring(capacity)..' UTF-8 bytes.'})
            end
        elseif kind==1 then
            checkProfanity=false
        end
        return openKeyboard(controller, title, value, limit, validateChars, checkProfanity, allowEmpty, callback, ...)
    end
    if ReplayOnline.Enabled() then
        local navigate = LUI.FlowManager.DoAutomaticNavigation
        if type(navigate) == "function" then
            LUI.FlowManager.DoAutomaticNavigation = function(manager, uiRoot, ...)
                -- The native dedicated session advertises root9 (LAN lobby).
                -- After admission, TU19 follows that root while the assigned
                -- map is preloading, replacing the online matchmaking menu.
                -- Keep an existing public lobby, or enter its stock builder for
                -- console connect (which has no playlist-button UI transition).
                -- Do not invoke native LAN navigation: it closes the online UI
                -- and changes mode. The wake guard below keeps the server map.
                local joining = ReplayOnline.SilentJoin()
                if (joining or (ReplayOnline.RestoringParty() and not ReplayOnline.LocalPlay())) and
                    (uiRoot == PartyUIRoot.MP_SYSTEM_LINK_LOBBY or uiRoot == PartyUIRoot.MP_SYSTEM_LINK) then
                    if manager.isPartyUIRootDirty then
                        ReplayOnline.Log("dedicated handoff retains online menu instead of following LAN root")
                    end
                    local lobby = CONDITIONS.IsMagmaGameMode() and "BRPublicLobby" or "MPPublicLobby"
                    if joining and not manager:StackContains(lobby) then
                        -- SetStack retains the existing Main/MPMainMenu parents.
                        -- Closing them first reruns their online initialization
                        -- and clears systemlink while this native route needs it.
                        manager:addMenu({menu = lobby, controller = Engine.IJEBHJIJF()})
                        ReplayOnline.Log("dedicated connect opened stock matchmaking lobby: " .. lobby)
                    end
                    manager.isPartyUIRootDirty = false
                    return true
                end
                return navigate(manager, uiRoot, ...)
            end
        end
        local awakeLobby = Lobby.DDDCGDHHJF
        local loggedJoinWake = false
        Lobby.DDDCGDHHJF = function(...)
            -- TU19 19E4160 sleeps PrivateParty and wakes GameParty (1189160).
            -- MP/BRPublicLobby calls it on rebuild. During our LAN-backed join,
            -- PrivateParty already owns the server map; waking the old local
            -- host chooses another playlist entry and cancels that preload.
            if ReplayOnline.SilentJoin() then
                if not loggedJoinWake then
                    ReplayOnline.Log("dedicated join retains server map: skipped local lobby wake")
                    loggedJoinWake = true
                end
                return
            end
            loggedJoinWake = false
            return awakeLobby(...)
        end
        local queuePath = "frontEnd.replayQueue.members"
        local models, values = {}, {}
        local function setModel(controller, path, value)
            if values[path] == value then return end
            local source = models[path] or LUI.DataSourceInGlobalModel.new(path)
            models[path] = source
            values[path] = value
            source:SetValue(controller, value)
        end
        local function queueState()
            local active, phase, seconds, count = ReplayOnline.QueueState()
            return active, tonumber(phase), tonumber(seconds), tonumber(count)
        end
        local function refreshQueue(controller)
            local active, phase, seconds, count = queueState()
            if not active then return false end
            setModel(controller, queuePath .. ".weaponCount", 0)
            for index = 0, count - 1 do
                local name, clan, xuid, rank, prestige, emblem, background, mine = ReplayOnline.QueuePlayer(index)
                if name then
                    local path = queuePath .. "." .. index .. "."
                    local fields = {
                        memberIndex = index, controllerIndex = mine and controller or -1,
                        isMyPlayer = mine, xuid = xuid, platformId = xuid,
                        isLobbyHost = false, isPrivatePartyHost = mine,
                        isVisible = true, team = 0, squad = 0, desiredTeam = 0,
                        selectedOperatorIndex = 0, bodyWest = 0, bodyEast = 0,
                        headWest = 0, headEast = 0, execWest = 0, execEast = 0,
                        rank = tonumber(rank), prestige = tonumber(prestige),
                        usingGamepad = false, gamertag = name, missingGamertag = false,
                        clanTag = clan ~= "" and ("[" .. clan .. "]") or "",
                        microphoneState = 0, subparty = index, presencePlatform = 4,
                        customEmblemEquipped = false, emblemIndex = tonumber(emblem),
                        backgroundIndex = tonumber(background), commitReady = false,
                        followersCount = 0, followerIndex = -1,
                        isCrossplayPlayer = false, isPlaceHolderPlayer = false
                    }
                    for field, value in pairs(fields) do setModel(controller, path .. field, value) end
                end
            end
            -- Publish count after every row exists: the stock grid subscribes to
            -- it and constructs native LobbyPlayer/LobbyGamertagPlayer widgets.
            setModel(controller, queuePath .. ".memberCount", count)
            setModel(controller, queuePath .. ".count", count)
            return true, phase, seconds, count
        end
        local buildGrid = MenuBuilder.m_types.lobbymembergrid
        MenuBuilder.m_types.lobbymembergrid = function(menu, options)
            if options and (options.modelPath == Lobby.AllMembersPath or options.modelPath == Lobby.SquadMembersPath)
                and refreshQueue(options.controllerIndex) then
                local args = {}
                for key, value in pairs(options) do args[key] = value end
                args.modelPath = queuePath
                local grid = buildGrid(menu, args)
                local timer = LUI.UITimer.new({interval = 500, event = "replay_waiting_roster"})
                grid:addElement(timer)
                grid:registerEventHandler("replay_waiting_roster", function()
                    if timer and not refreshQueue(args.controllerIndex) then
                        -- TU19 timers use UIElement.closeTree (also used by CRM).
                        -- close is absent and throws during the join transition.
                        timer:closeTree()
                        timer = nil
                    end
                end)
                return grid
            end
            return buildGrid(menu, options)
        end
        local foundLobby = Lobby.EBDDFIIHID
        Lobby.EBDDFIIHID = function(...)
            local active, phase, _, count = queueState()
            -- Keep TU19's SearchingForLobby panel while one player waits.
            -- The real roster takes over once another player arrives or the
            -- server starts loading. Do not fake native party/transport state.
            if active then return phase == 1 or phase == 2 or count > 1 end
            return foundLobby(...)
        end
        local matchmakingState = Lobby.DBJHBACBBE
        Lobby.DBJHBACBBE = function(...)
            local active, phase, _, count = queueState()
            if active then
                local found = phase == 1 or phase == 2 or count > 1
                return found and Lobby.MatchmakerState.JOINED_LOBBY or Lobby.MatchmakerState.SEARCHING, {}
            end
            return matchmakingState(...)
        end
        local statusString = Lobby.GetStatusString
        Lobby.GetStatusString = function(controller, ...)
            local active, phase, seconds, count = refreshQueue(controller)
            if active then
                if phase == 0 then
                    return string.format("Searching for a match - waiting for %d more player%s (%d:%02d)",
                        math.max(0, 2 - count), count == 1 and "" or "s",
                        math.floor(seconds / 60), seconds % 60), false, "waiting"
                end
                if phase == 1 then return "Loading match", true, "starting" end
                return "Match starting", true, "starting"
            end
            return statusString(controller, ...)
        end
        local function isJoiningPopup(name, data)
            if not ReplayOnline.SilentJoin() then return false end
            if name == "AcceptingInvite" or name == "AcceptingInviteWithoutCancel" then return true end
            return (name == "FenceDialogPopup" or name == "live_dialog_text_box") and data and
                data.message == Engine.CBBHFCGDIC("MENU/POPUP_ACCEPTINVITE")
        end
        local requestPopup = LUI.FlowManager.RequestPopupMenu
        LUI.FlowManager.RequestPopupMenu = function(menu, name, exclusive, controller, closeOthers, data, ...)
            if isJoiningPopup(name, data) then return end
            return requestPopup(menu, name, exclusive, controller, closeOthers, data, ...)
        end
        -- Live_OpenDialog injects a native addmenu event, bypassing the Lua
        -- RequestPopupMenu helper. Filter at the stock insertion point too;
        -- its event handler still performs the normal button-input unblock.
        local addMenu = LUI.FlowManager.addMenu
        LUI.FlowManager.addMenu = function(manager, event, ...)
            if event and isJoiningPopup(event.menu, event.data) then
                ReplayOnline.Log("matchmaking progress dialog suppressed: " .. event.menu)
                return
            end
            return addMenu(manager, event, ...)
        end
        -- Keep stock builders and error dialogs. Only the progress popup for a
        -- proxy-owned matchmaking join is skipped; direct LAN joins are normal.
        ReplayOnline.Log("stock matchmaking player list connected to waiting roster")
    end
    -- TU19 MainMenu's seasonal popup tests onboarding completion separately
    -- from the skip-video dvar. A fresh local profile reports it incomplete on
    -- every launch; honor the user's skip setting before the popup is queued.
    local seasonVideo = Onboarding and Onboarding.SeasonVideo
    if seasonVideo and type(seasonVideo.WasCompleted) == "function" then
        local wasCompleted = seasonVideo.WasCompleted
        seasonVideo.WasCompleted = function(self, controller, season, ...)
            if ReplayOnline.SkipIntroCinematics() and Dvar.IBEGCHEFE("NKMQMOKNRP") then
                -- Use the native tutorial flag/upload path once. Returning a
                -- permanent true without recording completion replays onboarding
                -- when another native caller consults the saved tutorial flags.
                if not wasCompleted(self, controller, season, ...) then
                    seasonVideo.MarkCompleted(self, controller, season)
                    ReplayOnline.Log("seasonal onboarding video completed without playback")
                end
                return true
            end
            return wasCompleted(self, controller, season, ...)
        end
    end
    if LUI.FlowManager and type(LUI.FlowManager.RequestPopupMenu) == "function" then
        local requestPopup = LUI.FlowManager.RequestPopupMenu
        LUI.FlowManager.RequestPopupMenu = function(menu, name, exclusive, controller, closeOthers, data, ...)
            -- PayBackOnBoardingLobby supplies the native callback which selects
            -- the playlist and waits for the public lobby. Defer it until the
            -- owning menu is built; never suppress a cinematic without its exit.
            if ReplayOnline.SkipIntroCinematics() and name == "CinematicFullscreen" and
                menu and (menu.id == "OnBoardingLobby" or menu.id == "PayBackOnBoardingLobby") and
                data and data.video == "mp_plunder_tutorial" and
                type(data.onVideoFinished) == "function" then
                if not menu._replayIntroCompletion then
                    menu._replayIntroCompletion = true
                    menu:Wait(1).onComplete = function()
                        ReplayOnline.Log("Plunder intro skipped; running native lobby completion")
                        data.onVideoFinished(controller)
                    end
                end
                return
            end
            return requestPopup(menu, name, exclusive, controller, closeOthers, data, ...)
        end
    end
    if not CP and PostGameFanfare then
        local advance = PostGameFanfare.advance
        PostGameFanfare.advance = function(self, ...)
            -- TU19 tests CP.SPEC_OPS after the summary even in MP. This build
            -- has no CP module: mark only that stage processed, then keep the
            -- native controller iteration, AAR cleanup and return-to-lobby flow.
            if self._state == PostGameFanfare.states.AAR_SUMMARY then
                self._state = PostGameFanfare.states.AAR_CP_POINTS
            end
            return advance(self, ...)
        end
    end
    local challengeButtons = MenuBuilder.m_types.challengemenubuttons
    MenuBuilder.m_types.challengemenubuttons = function(...)
        local element = challengeButtons(...)
        -- An empty current-week result lets the main Challenges page build,
        -- but the separate weekly tabs require dated assignments. Do not offer
        -- that submenu until the native clock can select a real week.
        if type(Engine.DEIDGHDBHD()) ~= "number" then
            element.WeekliesButton:SetButtonDisabled(true)
            element.WeekliesButton.Description:setText("Weekly challenges require online time synchronization.")
        end
        return element
    end
    -- MOTD content, counts, checksums and popups use the native CRM cache.

end
-- The in-game VM is separate from the frontend VM. BrPauseMenu builds
-- ChallengesSummaryBR, which uses this same timer before native UTC is ready.
-- Install the guard in both VMs so opening pause cannot trigger a LUI restart.
do
    local getWeeklyChallenges = Challenge.GetWeeklyChallenges
    local reportedWeeklyWait = false
    Challenge.GetWeeklyChallenges = function(...)
        -- The same nil UTC also crashes View Challenges via ChallengeMenu.
        -- Its native caller accepts no current week. Never fabricate a live
        -- assignment/date; immediately resume native data once UTC is ready.
        if type(Engine.DEIDGHDBHD()) ~= "number" then
            if not reportedWeeklyWait then
                reportedWeeklyWait = true
                ReplayOnline.Log("weekly challenges waiting for native UTC")
            end
            return {numWeeks = 0}, nil
        end
        return getWeeklyChallenges(...)
    end
    local setupDailyResetTimer = Challenge.SetupDailyResetTimer
    Challenge.SetupDailyResetTimer = function(element, countdownText, ...)
        -- Replay Engine.DEIDGHDBHD (RVA 0x19F59A0) returns nil until
        -- LiveStorage has synchronized UTC. The stock callback subtracts that
        -- value immediately. Keep the countdown blank until real time exists,
        -- then hand back to the unchanged native timer/calculation.
        if type(Engine.DEIDGHDBHD()) == "number" then
            return setupDailyResetTimer(element, countdownText, ...)
        end
        countdownText:setText("")
        if element._replayChallengeClockWait then return end
        -- Blanking Text alone leaves the native red Backer/Highlight visible.
        -- This timer is initially opaque; enclosing widget animations retain
        -- control of the panel's visibility while we wait for its real clock.
        element:SetAlpha(0)
        local args, count = {...}, select("#", ...)
        local event = "replay_challenge_clock_ready"
        local timer = LUI.UITimer.new({interval = 1000, event = event})
        element._replayChallengeClockWait = timer
        element:addElement(timer)
        element:addAndCallEventHandler(event, function(self)
            if not self._replayChallengeClockWait or type(Engine.DEIDGHDBHD()) ~= "number" then return end
            self._replayChallengeClockWait:closeTree()
            self._replayChallengeClockWait = nil
            self:SetAlpha(1)
            ReplayOnline.Log("native UTC ready; starting challenge reset countdown")
            setupDailyResetTimer(self, countdownText, unpack(args, 1, count))
        end)
        ReplayOnline.Log("challenge reset countdown waiting for native UTC")
    end
end
if ReplayOnline.Enabled() and Lobby and type(Lobby.LeavePublicLobby) == "function" then
    local leavePublicLobby = Lobby.LeavePublicLobby
    Lobby.LeavePublicLobby = function(...)
        ReplayOnline.Cancel()
        return leavePublicLobby(...)
    end
    ReplayOnline.Log("public lobby cancellation callback installed")
elseif ReplayOnline.Enabled() then
    ReplayOnline.Log("public lobby cancellation callback unavailable; needs native cancel mapping")
end
DebugPrint("[Replay] MP LUI startup complete")
ReplayOnline.Log("MP startup completed")
-- Temporary bounded enum diagnostics while recovering the remaining native
-- frontend prerequisites. No functions are called or values changed here.
for name, value in pairs(_G) do
    if type(name) == "string" and string.find(string.lower(name), "fence", 1, true) and type(value) == "table" then
        for key, constant in pairs(value) do
            if type(constant) == "number" or type(constant) == "string" then
                ReplayOnline.Log("enum " .. name .. "." .. tostring(key) .. "=" .. tostring(constant))
            end
        end
    end
end
