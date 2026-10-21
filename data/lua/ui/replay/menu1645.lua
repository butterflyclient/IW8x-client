-- Real 1.64.5 LuaFile bytecode, loaded only from a verified opt-in package.
-- Replay startup/requires stay native. This runs before frontend.lua and
-- client_settings.lua so their backend and pause wrappers see the final menus.
if not Engine.DDJFBBJAIG() or type(ReplayMenu1645) ~= "table" or
    type(ReplayMenu1645.Compile) ~= "function" then return end
if not MenuBuilder or not MenuBuilder.m_types or type(MAIN_MENU) ~= "table" then return end

-- Preserve TU19's predicates and import only the original 1.64.5 predicates
-- that the selected menu uses.  This original chunk only defines CONDITIONS;
-- invoking it with the table saved lets us retain all target behavior.
local missingConditions = {
    "AreWZLoadoutsEnabled", "CanDisplayNGBlade", "IsBattleNetModifyInstallEnabled",
    "IsBattlePassTopperActive", "IsBladeBannerActive", "IsCZStandardOrCrossGenOwned",
    "IsCZVaultOwned", "IsCdlEnabled", "IsCortezPreOrderBannerActive",
    "IsCortezPremiumPlayer", "IsCortezShowingPreOrderBannerOnItsBlade",
    "IsCrossLaunchDeepLinkEnabled", "IsEventPromoActive", "IsEventTabMoreInfoEnabled",
    "IsGiftingEnabled", "IsNewGameAvailableNowMessageActive",
    "IsOwnerOfCurrentUpsellTitle", "IsQABotSkipActive", "IsRiskGametype", "IsSeasonalEventActive",
    "IsSeasonalEventTabActive", "IsSony", "IsStoreRefreshTimerEnabled",
    "IsT9PremiumPlayer", "IsT9ZombieFreeTrialEnabled", "IsTrialEventAvailable",
    "IsVGPremiumPlayer", "IsVaguardTrialEnabled", "IsVanguardArenaDisabled",
    "IsVanguardDeeplinkEnabled", "IsVanguardOwnershipDisplayEnabled",
    "IsVanguardPreOrderBannerActive", "IsVanguardRevealEventActive",
    "IsVanguardRevealEventLive", "IsVanguardRevealed", "IsWZRatRace",
    "IsWZWipDvarEnabled", "IsWZWipDvarDisabled", "IsWZWipFlowEnabled", "IsWZWipOperatorFlowEnabled",
    "IsWebMotdEnabled", "SeasonalEventDisablesFTUE",
    "SeasonalEventHasParticipationChallenges", "ShouldCheckDLC",
    "ShowWZSeasonalOpeningScreenPC", "UserHasATexturePack"
}
local targetConditions = CONDITIONS
local sourceConditions = nil
local conditionChunk = ReplayMenu1645.Compile("ui/uieditor/conditions.lua")
if type(conditionChunk) ~= "function" then
    ReplayOnline.Log("original 1.64.5 condition bytecode missing; retaining TU19 UI")
    return
end

local conditionsOK, conditionsError = pcall(conditionChunk, "ui.uieditor.conditions")
sourceConditions = CONDITIONS
CONDITIONS = targetConditions
if not conditionsOK or type(sourceConditions) ~= "table" or type(targetConditions) ~= "table" then
    ReplayOnline.Log("original 1.64.5 conditions failed: " .. tostring(conditionsError))
    return
end
local addedConditions = {}
local conditionErrors = {}
-- Child widgets bring their own optional predicates. Import every missing
-- function from the verified table; existing TU19 predicates take precedence.
local listedConditions = {}
for _, name in ipairs(missingConditions) do listedConditions[name] = true end
for name, value in pairs(sourceConditions) do
    if type(name) == "string" and type(value) == "function" and
        targetConditions[name] == nil and not listedConditions[name] then
        missingConditions[#missingConditions + 1] = name
    end
end
for _, name in ipairs(missingConditions) do
    if targetConditions[name] == nil and type(sourceConditions[name]) == "function" then
        local original = sourceConditions[name]
        targetConditions[name] = function(...)
            local ok, result = pcall(original, ...)
            if ok then return result end
            if not conditionErrors[name] then
                conditionErrors[name] = true
                ReplayOnline.Log("1.64.5 condition unavailable on TU19: " .. name .. " (" .. tostring(result) .. ")")
            end
            return false
        end
        addedConditions[#addedConditions + 1] = name
    end
end
for _, name in ipairs(missingConditions) do
    if type(targetConditions[name]) ~= "function" then
        for _, added in ipairs(addedConditions) do targetConditions[added] = nil end
        ReplayOnline.Log("original 1.64.5 condition missing: " .. name .. "; retaining TU19 UI")
        return
    end
end

-- BEGIN target seasonal popup and playlist sound compatibility.
-- TU19 has no profile field for the later seasonal opening-screen revision.
-- Disable only that optional popup before its original predicate queries the
-- absent schema; the title, playlists and seasonal artwork remain available.
if targetConditions.ShowWZSeasonalOpeningScreenPC then
    targetConditions.ShowWZSeasonalOpeningScreenPC = function() return false end
end
-- The imported BR focus callback expects a sound slot added after TU19.
-- Retain TU19's original menu navigation alias for that interaction. Neither
-- the sound service nor the source widget's focus animation is replaced.
if SOUND_SETS and SOUND_SETS.mpMainMenu and SOUND_SETS.mpMainMenu.kbmSlide == nil then
    SOUND_SETS.mpMainMenu.kbmSlide = SOUND_SETS.mpMainMenu.moveDown
end
-- END target seasonal popup and playlist sound compatibility.

-- These four locations are defined by the original 1.64.5 crmutils.lua.
-- BEGIN target Warzone layout selection.
-- Original predicates distinguish the global UI style from the active game
-- mode. The refreshed title also needs the style while outside Warzone.
-- TU19's IsMagmaGameMode still owns the actual frontend/match mode.
if type(CONDITIONS.IsMagmaGameMode) == "function" then
    local nativeWarzoneMode = CONDITIONS.IsMagmaGameMode
    rawset(CONDITIONS, "IsWZWipDvarEnabled", function() return true end)
    rawset(CONDITIONS, "IsWZWipFlowEnabled", nativeWarzoneMode)
    rawset(CONDITIONS, "IsWZWipDvarDisabled", function() return false end)
end
-- END target Warzone layout selection.

-- Opting into this package selects the original refreshed title layout.
-- This source UI flag has no TU19 equivalent; all game/service dvars still
-- use the target getter. Its original blade image set is in the menu FF.
local targetUIBool = Dvar.IBEGCHEFE
rawset(Dvar, "IBEGCHEFE", function(name, ...)
    if name == "wz_enable_blades_refresh" or name == "MLQNQTRRTK" then return true end
    return targetUIBool(name, ...)
end)
local targetUIString = Dvar.DHEEJCCJBH
rawset(Dvar, "DHEEJCCJBH", function(name, ...)
    if name == "NKNPQRNMST" then
        -- The source publisher selected the BR blade movie through this
        -- later string dvar, absent on TU19. Use the supplied original movie
        -- for the selected three-blade layout, never a nil native argument.
        return "main_menu_3blade_wz"
    end
    return targetUIString(name, ...)
end)

-- The source BR menu links focus from the last playlist entry to its squad
-- controls. Original ui/luilist.lua walks native sibling links here; TU19
-- already owns those links but omits this convenience method.
if type(LUI.List) == "table" and type(LUI.List.GetLastElement) ~= "function" then
    rawset(LUI.List, "GetLastElement", function(self)
        local current, last = self:getFirstChild(), nil
        while current do
            last = current
            current = current:getNextSibling()
        end
        return last
    end)
end

-- These four locations are defined by the original 1.64.5 crmutils.lua.
-- TU19 has no live-event blade locations, and the original child widget
-- indexes its location set at module load time. Preserve existing CRM keys.
local originalLiveEventLocations = {
    NG_LIVE_EVENT = 27,
    CW_LIVE_EVENT = 24,
    MW_LIVE_EVENT = 22,
    WZ_LIVE_EVENT = 23
}
local addedCrmLocations = {}
if type(CRM) == "table" and type(CRM.locations) == "table" then
    for name, value in pairs(originalLiveEventLocations) do
        if CRM.locations[name] == nil then
            CRM.locations[name] = value
            addedCrmLocations[#addedCrmLocations + 1] = name
        end
    end
else
    for _, added in ipairs(addedConditions) do targetConditions[added] = nil end
    ReplayOnline.Log("TU19 CRM locations unavailable; retaining TU19 UI")
    return
end

-- The 1.64.5 title toggles the root cursor's force-visible flag. This method
-- is in that build's ui/luimousecursor.lua, while TU19's cursor omits it.
local addedCursorMethod = false
if type(LUI.UIMouseCursor) == "table" and
    type(LUI.UIMouseCursor.ForceCursorVisibility) ~= "function" then
    rawset(LUI.UIMouseCursor, "ForceCursorVisibility", function(visible)
        if type(Engine.DBFFAEEFGJ) ~= "function" then return end
        local root = Engine.DBFFAEEFGJ()
        if root and root.cursor then root.cursor._forceIsVisible = visible end
    end)
    addedCursorMethod = true
end

-- The source luielement.lua adds these thin prompt aliases. TU19 already has
-- the underlying helper-text event methods, so keep its element class and add
-- only the two original forwarding methods used by the imported menus.
local addedElementMethods = {}
if type(LUI.UIElement) == "table" then
    if type(LUI.UIElement.AddButtonPrompt) ~= "function" then
        rawset(LUI.UIElement, "AddButtonPrompt", function(self, prompt, options)
            return self:AddButtonHelperTextToElement(
                (options and options.menu) or self:GetCurrentMenu(), prompt)
        end)
        addedElementMethods[#addedElementMethods + 1] = "AddButtonPrompt"
    end
    if type(LUI.UIElement.RemoveButtonPrompt) ~= "function" then
        rawset(LUI.UIElement, "RemoveButtonPrompt", function(self, prompt, options)
            return self:RemoveButtonHelperTextFromElement(
                (options and options.menu) or self:GetCurrentMenu(),
                prompt.button_ref, prompt.side)
        end)
        addedElementMethods[#addedElementMethods + 1] = "RemoveButtonPrompt"
    end
end

-- Source 1.64.5 TabManager adds a switch notification used by its MP tabs.
-- Keep TU19's tab construction/navigation and add that original event shape
-- only to managers whose imported MP menu opts in.
local addedTabManagerFields = {}
if type(LUI.TabManager) == "table" then
    if LUI.TabManager.TAB_SWITCH_EVENT == nil then
        rawset(LUI.TabManager, "TAB_SWITCH_EVENT", "tab_switch")
        addedTabManagerFields[#addedTabManagerFields + 1] =
            {"TAB_SWITCH_EVENT", nil}
    end
    if type(LUI.TabManager.SetSendEventOnTabChange) ~= "function" then
        rawset(LUI.TabManager, "SetSendEventOnTabChange", function(self, enabled)
            self._sendTabSwitchEvent = enabled
        end)
        addedTabManagerFields[#addedTabManagerFields + 1] =
            {"SetSendEventOnTabChange", nil}
        local targetFocusTab = LUI.TabManager.FocusTab
        if type(targetFocusTab) == "function" then
            rawset(LUI.TabManager, "FocusTab", function(self, controller, index, focusType)
                local before = self._currentTab
                local result = targetFocusTab(self, controller, index, focusType)
                if self._sendTabSwitchEvent and self._currentTab ~= before and
                    type(self.dispatchEventToParent) == "function" then
                    self:dispatchEventToParent({
                        name = LUI.TabManager.TAB_SWITCH_EVENT,
                        controller = controller,
                        index = self._currentTab
                    })
                end
                return result
            end)
            addedTabManagerFields[#addedTabManagerFields + 1] =
                {"FocusTab", targetFocusTab}
        end
    end
end

-- The later MP parent stores its selected lobby track in this global model.
-- TU19's parent plays its track directly and has no model field; the same
-- target data-source class supplies the original 1.64.5 setter contract.
local addedLobbyMusic = false
if type(DataSources) == "table" and type(DataSources.frontEnd) == "table" and
    type(DataSources.frontEnd.lobby) == "table" and
    DataSources.frontEnd.lobby.lobbyMusic == nil and
    type(LUI.DataSourceInGlobalModel) == "table" and
    type(LUI.DataSourceInGlobalModel.new) == "function" then
    rawset(DataSources.frontEnd.lobby, "lobbyMusic",
        LUI.DataSourceInGlobalModel.new("frontEnd.lobby.lobbyMusic"))
    addedLobbyMusic = true
end

-- The original 1.64 lobby helper defers character updates until four scene
-- updates complete, then calls the supplied completion with the character
-- map. TU19's UpdateClientCharacters performs those updates synchronously
-- and returns that same map; deliver the original completion at that point.
local addedDelayedCharacters = false
if type(Lobby) == "table" and
    type(Lobby.UpdateClientCharactersDelayed) ~= "function" and
    type(Lobby.UpdateClientCharacters) == "function" then
    local updateTargetCharacters = Lobby.UpdateClientCharacters
    rawset(Lobby, "UpdateClientCharactersDelayed",
        function(widget, controller, options, onComplete)
            local characterMap = updateTargetCharacters(controller, options)
            if type(onComplete) == "function" then onComplete(characterMap) end
            return characterMap
        end)
    addedDelayedCharacters = true
end

-- The source title resets its web MOTD context when opening. Replay TU19 has
-- no WebViewManager or web MOTD surface, so this context is empty here.
local previousWebViewManager = type(LUI) == "table" and LUI.WebViewManager or nil
local addedWebViewManager = false
if type(LUI) == "table" and type(previousWebViewManager) ~= "table" then
    local emptyContext = {ResetContext = function() end}
    rawset(LUI, "WebViewManager", {
        Instance = function() return emptyContext end,
        MotdEnabled = function() return false end
    })
    addedWebViewManager = true
end

-- Original luashared/cod.lua adds these seasonal profile-reset helpers. The
-- TU19 profile schema has no matching ranked/event fields, so do not write
-- source-era player data merely to construct the imported title screen.
local addedSeasonalHelpers = {}
if type(CoD) == "table" then
    for _, name in ipairs({"CheckAndApplyResetSeasonalPlayerData",
                          "CheckAndApplySeasonalEventPlayerData"}) do
        if type(CoD[name]) ~= "function" then
            rawset(CoD, name, function() end)
            addedSeasonalHelpers[#addedSeasonalHelpers + 1] = name
        end
    end
end

-- The source MainMenu calls this later native hook immediately after
-- BHHAAHFCFC(). The TU19 MainMenu has the same surrounding calls and omits
-- this hook; preserve its constructor sequence without inventing engine work.
local addedTitleNativeHook = false
if type(Engine) == "table" and type(Engine.DDEJECEIBB) ~= "function" then
    rawset(Engine, "DDEJECEIBB", function() end)
    addedTitleNativeHook = true
end

-- The later title checks for DLC-remaster and console texture-pack popups.
-- TU19 has none of these native products/services; false keeps those popups
-- closed while leaving every existing target native entry untouched.
local addedPopupNativeChecks = {}
if type(Engine) == "table" then
    for _, name in ipairs({"DGGIJADHJI", "DHHIJGFHF", "CJEHADCJDB",
                          "BJBBFFIFAC", "IDICJHGG", "BDFBGADHAH"}) do
        if type(Engine[name]) ~= "function" then
            rawset(Engine, name, function() return false end)
            addedPopupNativeChecks[#addedPopupNativeChecks + 1] = name
        end
    end
end

-- The source SeasonUtils reads this current-season dvar for ordinary menu
-- checks. TU19 retains the same dvar but not the later Lua helper; its
-- Battle Pass override dvar is absent, so retain only the shared path.
local addedSeasonNumber = false
if type(SEASON) == "table" and type(SEASON.GetCurrentSeasonDataNum) ~= "function" then
    rawset(SEASON, "GetCurrentSeasonDataNum", function()
        return Dvar.CFHDGABACF("LRTSSKLKPK")
    end)
    addedSeasonNumber = true
end
local addedSeasonDisplayNumber = false
if type(SEASON) == "table" and type(SEASON.GetCurrentSeasonDisplayNum) ~= "function" then
    -- Original SeasonUtils groups six numbered seasons per premium product.
    -- The title blade uses this for its localized season label.
    rawset(SEASON, "GetCurrentSeasonDisplayNum", function(options)
        local dataNumber = (options and options.seasonDataNumToUse) or
            SEASON.GetCurrentSeasonDataNum(options)
        if type(dataNumber) == "number" and dataNumber > 6 then
            return (dataNumber - 1) % 6 + 1
        end
        return dataNumber
    end)
    addedSeasonDisplayNumber = true
end

-- The 1.64 title asks whether a mid-season cinematic should open. Its
-- original WasCompleted() returns true when the season CSV has no video name;
-- TU19 has no mid-season video column or onboarding entry.
local addedMidSeasonVideo = false
if type(Onboarding) == "table" and Onboarding.MidSeasonVideo == nil then
    rawset(Onboarding, "MidSeasonVideo", {WasCompleted = function() return true end})
    addedMidSeasonVideo = true
end
-- The refreshed title reads this newer onboarding entry before checking its
-- event flag. TU19 has no record for that event. Keep the intro-skip setting
-- as presentation policy; never create or save a later-era profile field.
local addedReverseNukeVideo = false
if type(Onboarding) == "table" and Onboarding.ReverseNukeVideo == nil then
    rawset(Onboarding, "ReverseNukeVideo", {WasCompleted = function()
        return ReplayOnline.SkipIntroCinematics()
    end})
    addedReverseNukeVideo = true
end

-- TU19 BRMainMenu sets this exact native map literal in menu_create. The
-- newer menu moved it into BR.DEFAULT_MAP; retain the target's default rather
-- than asking the engine to set a nil string or selecting an unported map.
local addedBRDefaultMap = type(BR) == "table" and BR.DEFAULT_MAP == nil
if addedBRDefaultMap then rawset(BR, "DEFAULT_MAP", "mp_donetsk") end

-- The source WZ/MP widgets use the 1.64 wzwiputils.lua helpers. Their two
-- definitions do not exist in the TU19 ui.ff; preserve any target methods.
local previousWzwip = WZWIP
local wzwip = previousWzwip
if type(wzwip) ~= "table" then
    wzwip = {}
    rawset(_G, "WZWIP", wzwip)
end
local addedWzwip = {}
if type(wzwip.CheckForApplyPrependWZWipPrefix) ~= "function" then
    if wzwip.Prefix == nil then
        rawset(wzwip, "Prefix", "WZWip")
        addedWzwip[#addedWzwip + 1] = "Prefix"
    end
    rawset(wzwip, "CheckForApplyPrependWZWipPrefix", function(sequence, force)
        if CONDITIONS.IsMagmaGameMode() or force then
            return wzwip.Prefix .. sequence
        end
        return sequence
    end)
    addedWzwip[#addedWzwip + 1] = "CheckForApplyPrependWZWipPrefix"
end
if type(wzwip.AnimateThemeElement) ~= "function" then
    rawset(wzwip, "AnimateThemeElement", function(element, sequence, options)
        local useWzwip = CONDITIONS.IsWZWipDvarEnabled()
        if options and options.doSnapToState then sequence = sequence .. "Snap" end
        if options and options.conditionOverride then useWzwip = options.conditionOverride end
        if options and options.resizeSmallIfLanguage then
            local predicates = options.resizeSmallIfLanguage
            if type(predicates) == "function" then predicates = {predicates} end
            for _, predicate in ipairs(predicates) do
                if predicate() then sequence = sequence .. "Small"; break end
            end
        end
        if options and options.forceSmall then sequence = sequence .. "Small" end
        sequence = wzwip.CheckForApplyPrependWZWipPrefix(sequence, useWzwip)
        if element._sequences and element._sequences[sequence] then
            ACTIONS.AnimateSequence(element, sequence)
        end
    end)
    addedWzwip[#addedWzwip + 1] = "AnimateThemeElement"
end

-- The original 1.64.5 title and MP menus use later chapter swatches absent
-- from TU19. Execute the exact original data-only colors/swatches LuaFiles in
-- temporary globals, then copy only missing fields in referenced groups.
-- Existing Replay values retain precedence, including Client Settings colors.
local referencedSwatchGroups = {
    "AAR", "BattlePass", "BLM", "BRMapItemColor", "CAC", "CH2",
    "Dismemberment", "Frontend", "genericButton", "genericMenu", "Global", "HUD",
    "itemRarity", "loot", "MainMenu", "Reticles", "Store", "text", "Tracer",
    "WZCH3", "WZGlobalSwatches"
}
local addedSwatchGroups = {}
local addedSwatchFields = {}
local originalPaletteColors = nil
local function importOriginalSwatches()
    local colorsChunk = ReplayMenu1645.Compile("ui/uieditor/colors.lua")
    local swatchesChunk = ReplayMenu1645.Compile("ui/uieditor/swatches.lua")
    if type(colorsChunk) ~= "function" or type(swatchesChunk) ~= "function" or
        type(COLORS) ~= "table" or type(SWATCHES) ~= "table" then
        error("original 1.64.5 palette or TU19 palette is unavailable")
    end
    local targetColors, targetSwatches = COLORS, SWATCHES
    local colorsOK, colorsError = pcall(colorsChunk, "ui.uieditor.colors_magma1645")
    local sourceColors = COLORS
    COLORS = targetColors
    if not colorsOK or type(sourceColors) ~= "table" then
        error("original 1.64.5 colors failed: " .. tostring(colorsError))
    end
    originalPaletteColors = sourceColors
    COLORS = sourceColors
    local swatchesOK, swatchesError = pcall(swatchesChunk, "ui.uieditor.swatches_magma1645")
    local sourceSwatches = SWATCHES
    COLORS, SWATCHES = targetColors, targetSwatches
    if not swatchesOK or type(sourceSwatches) ~= "table" then
        error("original 1.64.5 swatches failed: " .. tostring(swatchesError))
    end
    local listedGroups = {}
    for _, name in ipairs(referencedSwatchGroups) do listedGroups[name] = true end
    for name, value in pairs(sourceSwatches) do
        if type(name) == "string" and type(value) == "table" and not listedGroups[name] then
            referencedSwatchGroups[#referencedSwatchGroups + 1] = name
        end
    end
    for _, groupName in ipairs(referencedSwatchGroups) do
        local sourceGroup = sourceSwatches[groupName]
        if type(sourceGroup) ~= "table" then
            error("original 1.64.5 swatch group missing: " .. groupName)
        end
        local targetGroup = targetSwatches[groupName]
        if targetGroup == nil then
            targetGroup = {}
            rawset(targetSwatches, groupName, targetGroup)
            addedSwatchGroups[#addedSwatchGroups + 1] = groupName
        elseif type(targetGroup) ~= "table" then
            error("TU19 swatch group has unexpected type: " .. groupName)
        end
        for fieldName, value in pairs(sourceGroup) do
            if targetGroup[fieldName] == nil then
                rawset(targetGroup, fieldName, value)
                addedSwatchFields[#addedSwatchFields + 1] = {targetGroup, fieldName}
            end
        end
    end
end

-- The original title's button_start handler updates its selected UI theme.
-- TU19 has no THEMES table, so load the source theme definitions and methods
-- against the original color data, then restore the target COLORS/SWATCHES.
local previousThemes = THEMES
local addedThemesGlobal = false
local addedThemeFields = {}
local function importOriginalThemes()
    local swatchThemesChunk = ReplayMenu1645.Compile("ui/uieditor/swatchthemes.lua")
    local themeUtilChunk = ReplayMenu1645.Compile("ui/utils/themeutil.lua")
    if type(swatchThemesChunk) ~= "function" or type(themeUtilChunk) ~= "function" or
        type(originalPaletteColors) ~= "table" then
        error("original 1.64.5 theme bytecode or colors unavailable")
    end
    local targetColors, targetThemes = COLORS, THEMES
    COLORS = originalPaletteColors
    local definitionsOK, definitionsError = pcall(swatchThemesChunk,
        "ui.uieditor.swatchthemes_magma1645")
    local sourceThemes = THEMES
    THEMES = targetThemes
    if not definitionsOK or type(sourceThemes) ~= "table" then
        COLORS = targetColors
        error("original 1.64.5 theme definitions failed: " .. tostring(definitionsError))
    end
    THEMES = sourceThemes
    local utilityOK, utilityError = pcall(themeUtilChunk, "ui.utils.themeutil_magma1645")
    sourceThemes = THEMES
    COLORS, THEMES = targetColors, targetThemes
    if not utilityOK or type(sourceThemes) ~= "table" or
        type(sourceThemes.UpdateTheme) ~= "function" then
        error("original 1.64.5 theme utility failed: " .. tostring(utilityError))
    end
    if type(targetThemes) == "table" then
        for name, value in pairs(sourceThemes) do
            if targetThemes[name] == nil then
                rawset(targetThemes, name, value)
                addedThemeFields[#addedThemeFields + 1] = name
            end
        end
    else
        THEMES = sourceThemes
        addedThemesGlobal = true
    end
end

-- The source title creates an Alpha/Beta countdown, and reachable MP widgets
-- use the same later TIME helper. Import its original LuaFile rather than
-- recreating timer events. An absent or expired event dvar has no countdown.
local addedTimeFields = {}
local function importOriginalTime()
    local timeChunk = ReplayMenu1645.Compile("ui/utils/timeutils.lua")
    local targetTime = TIME
    if type(timeChunk) ~= "function" or type(targetTime) ~= "table" then
        error("original 1.64.5 time utility or TU19 TIME table is unavailable")
    end
    local timeOK, timeError = pcall(timeChunk, "ui.utils.timeutils_magma1645")
    local sourceTime = TIME
    TIME = targetTime
    if not timeOK or type(sourceTime) ~= "table" then
        error("original 1.64.5 time utility failed: " .. tostring(timeError))
    end
    local missingCountdownLogged = false
    for name, value in pairs(sourceTime) do
        if targetTime[name] == nil then
            if name == "SetCountDownTimerTextField" then
                local originalCountdown = value
                value = function(field, expiration, options)
                    if type(expiration) ~= "number" or expiration <= 0 then
                        if field and type(field.setText) == "function" then
                            field:setText("")
                        end
                        if not missingCountdownLogged then
                            missingCountdownLogged = true
                            ReplayOnline.Log("1.64.5 event countdown has no valid target time on TU19")
                        end
                        return
                    end
                    return originalCountdown(field, expiration, options)
                end
            end
            rawset(targetTime, name, value)
            addedTimeFields[#addedTimeFields + 1] = name
        end
    end
    if type(targetTime.SetCountDownTimerTextField) ~= "function" then
        error("original 1.64.5 countdown helper did not register")
    end
end

-- The original Play and Weapons panels call layout methods introduced after
-- TU19. Execute the exact 1.64.5 utility bytecode against a temporary table,
-- then copy only missing fields; Replay keeps all of its existing layout
-- methods and values.
local addedLayoutFields = {}
local function importOriginalLayout()
    local layoutChunk = ReplayMenu1645.Compile("ui/utils/layoututils.lua")
    local targetLayout = LAYOUT
    if type(layoutChunk) ~= "function" or type(targetLayout) ~= "table" then
        error("original 1.64.5 layout utility or TU19 LAYOUT table is unavailable")
    end
    LAYOUT = {}
    local layoutOK, layoutError = pcall(layoutChunk,
        "ui.utils.layoututils_magma1645")
    local sourceLayout = LAYOUT
    LAYOUT = targetLayout
    if not layoutOK or type(sourceLayout) ~= "table" then
        error("original 1.64.5 layout utility failed: " .. tostring(layoutError))
    end
    for name, value in pairs(sourceLayout) do
        if targetLayout[name] == nil then
            rawset(targetLayout, name, value)
            addedLayoutFields[#addedLayoutFields + 1] = name
        end
    end
    if type(targetLayout.SetupScalingBackgroundVignette) ~= "function" then
        error("original 1.64.5 scaling vignette helper did not register")
    end
end

-- The later tab bar queries a custom-loadout predicate that TU19 lacks.
-- Keep Replay's real progression/profile table and its IsUnlocked logic;
-- import only the original 1.64.5 predicate from the verified source chunk.
local addedCustomLoadoutPredicate = false
local function importOriginalCustomLoadoutPredicate()
    local progressionChunk = ReplayMenu1645.Compile("ui/utils/progressionutils.lua")
    local targetProgression = PROGRESSION
    if type(progressionChunk) ~= "function" or
        type(targetProgression) ~= "table" or
        type(targetProgression.IsUnlocked) ~= "function" then
        error("original progression utility or TU19 unlock state is unavailable")
    end
    PROGRESSION = {}
    local progressionOK, progressionError = pcall(progressionChunk,
        "ui.utils.progressionutils_magma1645")
    local sourceProgression = PROGRESSION
    PROGRESSION = targetProgression
    if not progressionOK or type(sourceProgression) ~= "table" or
        type(sourceProgression.AreCustomLoadoutsUnlocked) ~= "function" then
        error("original 1.64.5 custom-loadout predicate failed: " ..
            tostring(progressionError))
    end
    if targetProgression.AreCustomLoadoutsUnlocked == nil then
        local original = sourceProgression.AreCustomLoadoutsUnlocked
        local loggedFallback = false
        rawset(targetProgression, "AreCustomLoadoutsUnlocked", function(controller)
            local ok, unlocked = pcall(original, controller)
            if ok then return unlocked end
            if not loggedFallback then
                loggedFallback = true
                ReplayOnline.Log("1.64.5 custom-loadout predicate used TU19 " ..
                    "unlock fallback: " .. tostring(unlocked))
            end
            return targetProgression.IsUnlocked(controller,
                LOOT.itemTypes.FEATURE, LOOT.createAClassRef)
        end)
        addedCustomLoadoutPredicate = true
    end
end

-- The source MP tab bar calls this later store timer utility during setup.
-- Execute its exact original bytecode against a scratch STORE table, then
-- publish only GetSaleTimer to the target table. This keeps TU19's existing
-- catalog, entitlement, and purchase behavior intact.
local addedStoreSaleTimer = false
local addedVanguardAvailability = false
local function importOriginalStoreSaleTimer()
    local storeChunk = ReplayMenu1645.Compile("ui/utils/storeutils.lua")
    local targetStore = STORE
    if type(storeChunk) ~= "function" or type(targetStore) ~= "table" then
        error("original store utility or TU19 STORE table is unavailable")
    end
    STORE = setmetatable({}, {__index = targetStore})
    local storeOK, storeError = pcall(storeChunk,
        "ui.utils.storeutils_magma1645")
    local sourceStore = STORE
    STORE = targetStore
    if not storeOK or type(sourceStore) ~= "table" or
        type(sourceStore.GetSaleTimer) ~= "function" or
        type(sourceStore.IsVanguardAvailable) ~= "function" then
        error("original 1.64.5 store utility failed: " ..
            tostring(storeError))
    end
    if targetStore.GetSaleTimer == nil then
        local original = sourceStore.GetSaleTimer
        local loggedUnavailable = false
        rawset(targetStore, "GetSaleTimer", function(controller)
            local ok, timer = pcall(original, controller)
            if ok then return timer end
            if not loggedUnavailable then
                loggedUnavailable = true
                ReplayOnline.Log("1.64.5 sale timer unavailable on TU19: " ..
                    tostring(timer))
            end
            return nil
        end)
        addedStoreSaleTimer = true
    end
    if targetStore.IsVanguardAvailable == nil then
        local original = sourceStore.IsVanguardAvailable
        local loggedUnavailable = false
        rawset(targetStore, "IsVanguardAvailable", function(controller)
            local ok, available = pcall(original, controller)
            if ok then return available end
            if not loggedUnavailable then
                loggedUnavailable = true
                ReplayOnline.Log("1.64.5 Vanguard store availability " ..
                    "unavailable on TU19: " .. tostring(available))
            end
            return false
        end)
        addedVanguardAvailability = true
    end
end

-- The later MP menu repairs trial reward state on entry. TU19 has its trials
-- table but not FixUpLoot. Keep the original helper and its source-local
-- trial functions together for the synchronous call; an unavailable later
-- trial service skips that repair and leaves the target profile untouched.
local addedTrialsFixup = false
local function importOriginalTrialsFixup()
    local trialsChunk = ReplayMenu1645.Compile("ui/utils/trialsutils.lua")
    local targetTrials = TRIALS
    if type(trialsChunk) ~= "function" or type(targetTrials) ~= "table" then
        error("original trials utility or TU19 TRIALS table is unavailable")
    end
    TRIALS = setmetatable({}, {__index = targetTrials})
    local trialsOK, trialsError = pcall(trialsChunk,
        "ui.utils.trialsutils_magma1645")
    local sourceTrials = TRIALS
    TRIALS = targetTrials
    if not trialsOK or type(sourceTrials) ~= "table" or
        type(sourceTrials.FixUpLoot) ~= "function" then
        error("original 1.64.5 trial-loot helper failed: " ..
            tostring(trialsError))
    end
    if targetTrials.FixUpLoot == nil then
        local original = sourceTrials.FixUpLoot
        local loggedUnavailable = false
        rawset(targetTrials, "FixUpLoot", function(controller)
            local previous = TRIALS
            TRIALS = sourceTrials
            local ok, result = pcall(original, controller)
            TRIALS = previous
            if ok then return result end
            if not loggedUnavailable then
                loggedUnavailable = true
                ReplayOnline.Log("1.64.5 trial-loot repair unavailable " ..
                    "on TU19: " .. tostring(result))
            end
            return nil
        end)
        addedTrialsFixup = true
    end
end

-- Original MP entry validates equipped cosmetics after the trial check. Keep
-- each source utility's own helper table for its synchronous validation call
-- and leave Replay's working LOOT/OPERATOR tables in place for the rest of UI.
local addedLootCustomizationCheck = false
local addedOperatorCustomizationCheck = false
local addedWeaponOwnershipCheck = false
local addedLootBreadcrumbMethods = {}
local addedGameSourceID = false
local addedGameSourceIDFields = {}
local function importOriginalCustomizationChecks()
    local soundChunk = ReplayMenu1645.Compile("ui/uieditor/soundsets.lua")
    local lootChunk = ReplayMenu1645.Compile("ui/utils/lootutils.lua")
    local operatorChunk = ReplayMenu1645.Compile("ui/utils/operatorutils.lua")
    local weaponChunk = ReplayMenu1645.Compile("ui/utils/weaponutils.lua")
    local targetLoot, targetOperator, targetWeapon = LOOT, OPERATOR, WEAPON
    local targetSoundSets = SOUND_SETS
    if type(soundChunk) ~= "function" or type(lootChunk) ~= "function" or
        type(operatorChunk) ~= "function" or type(weaponChunk) ~= "function" or
        type(LUI) ~= "table" or
        type(targetSoundSets) ~= "table" or
        type(targetLoot) ~= "table" or type(targetOperator) ~= "table" or
        type(targetWeapon) ~= "table" then
        error("original customization utilities or TU19 state are unavailable")
    end
    -- Original ui/utils/lui.lua defines this exact four-value enum. Replay
    -- TU19 has no GAME_SOURCE_ID, and source LOOT initializes keyed tables
    -- from it before either customization method can be registered.
    local originalGameSourceID = {
        WARZONE = "wz", COLD_WAR = "t9", VANGUARD = "s4",
        MODERN_WARFARE = "iw8"
    }
    local targetGameSourceID = LUI.GAME_SOURCE_ID
    if targetGameSourceID == nil then
        rawset(LUI, "GAME_SOURCE_ID", originalGameSourceID)
        addedGameSourceID = true
    elseif type(targetGameSourceID) == "table" then
        for name, value in pairs(originalGameSourceID) do
            if targetGameSourceID[name] == nil then
                rawset(targetGameSourceID, name, value)
                addedGameSourceIDFields[#addedGameSourceIDFields + 1] = name
            end
        end
    else
        error("TU19 game-source IDs have an unexpected type")
    end
    local soundOK, soundError = pcall(soundChunk,
        "ui.uieditor.soundsets_magma1645")
    local sourceSoundSets = SOUND_SETS
    SOUND_SETS = targetSoundSets
    if not soundOK or type(sourceSoundSets) ~= "table" or
        type(sourceSoundSets.battlePass) ~= "table" then
        error("original 1.64.5 sound sets failed: " .. tostring(soundError))
    end
    -- The source LOOT table initializes rarity sound references at load time.
    -- Replay TU19 lacks its later battle-pass set, so expose the exact source
    -- data only while constructing the scratch source table.
    SOUND_SETS = setmetatable({battlePass = sourceSoundSets.battlePass},
        {__index = targetSoundSets})
    LOOT = setmetatable({}, {__index = targetLoot})
    local lootOK, lootError = pcall(lootChunk,
        "ui.utils.lootutils_magma1645")
    local sourceLoot = LOOT
    LOOT = targetLoot
    SOUND_SETS = targetSoundSets
    if not lootOK or type(sourceLoot) ~= "table" or
        type(sourceLoot.CheckCustomizationsShouldBeEquipped) ~= "function" then
        error("original 1.64.5 loot customization utility failed: " ..
            tostring(lootError))
    end
    OPERATOR = setmetatable({}, {__index = targetOperator})
    local operatorOK, operatorError = pcall(operatorChunk,
        "ui.utils.operatorutils_magma1645")
    local sourceOperator = OPERATOR
    OPERATOR = targetOperator
    if not operatorOK or type(sourceOperator) ~= "table" or
        type(sourceOperator.CheckCustomizationsShouldBeEquipped) ~= "function" then
        error("original 1.64.5 operator customization utility failed: " ..
            tostring(operatorError))
    end
    WEAPON = setmetatable({}, {__index = targetWeapon})
    local weaponOK, weaponError = pcall(weaponChunk,
        "ui.utils.weaponutils_magma1645")
    local sourceWeapon = WEAPON
    WEAPON = targetWeapon
    if not weaponOK or type(sourceWeapon) ~= "table" or
        type(sourceWeapon.CheckWeaponsShouldBeEquipped) ~= "function" then
        error("original 1.64.5 weapon ownership utility failed: " ..
            tostring(weaponError))
    end
    if targetLoot.CheckCustomizationsShouldBeEquipped == nil then
        local original = sourceLoot.CheckCustomizationsShouldBeEquipped
        local loggedUnavailable = false
        rawset(targetLoot, "CheckCustomizationsShouldBeEquipped",
            function(controller)
                local previous = LOOT
                LOOT = sourceLoot
                local ok, result = pcall(original, controller)
                LOOT = previous
                if ok then return result end
                if not loggedUnavailable then
                    loggedUnavailable = true
                    ReplayOnline.Log("1.64.5 loot customization check " ..
                        "unavailable on TU19: " .. tostring(result))
                end
                return nil
            end)
        addedLootCustomizationCheck = true
    end
    if targetOperator.CheckCustomizationsShouldBeEquipped == nil then
        local original = sourceOperator.CheckCustomizationsShouldBeEquipped
        local loggedUnavailable = false
        rawset(targetOperator, "CheckCustomizationsShouldBeEquipped",
            function(controller)
                local previous, previousLoot = OPERATOR, LOOT
                OPERATOR = sourceOperator
                LOOT = sourceLoot
                local ok, result = pcall(original, controller)
                OPERATOR, LOOT = previous, previousLoot
                if ok then return result end
                if not loggedUnavailable then
                    loggedUnavailable = true
                    ReplayOnline.Log("1.64.5 operator customization check " ..
                        "unavailable on TU19: " .. tostring(result))
                end
                return nil
            end)
        addedOperatorCustomizationCheck = true
    end
    if targetWeapon.CheckWeaponsShouldBeEquipped == nil then
        local original = sourceWeapon.CheckWeaponsShouldBeEquipped
        local loggedUnavailable = false
        rawset(targetWeapon, "CheckWeaponsShouldBeEquipped",
            function(controller)
                local previous, previousLoot = WEAPON, LOOT
                WEAPON, LOOT = sourceWeapon, sourceLoot
                local ok, result = pcall(original, controller)
                WEAPON, LOOT = previous, previousLoot
                if ok then return result end
                if not loggedUnavailable then
                    loggedUnavailable = true
                    ReplayOnline.Log("1.64.5 weapon ownership check " ..
                        "unavailable on TU19: " .. tostring(result))
                end
                return nil
            end)
        addedWeaponOwnershipCheck = true
    end
    -- The original MP tab constructor and Weapons/Operators pages call two
    -- later breadcrumb helpers missing from the TU19 LOOT utility. Use their
    -- exact original bytecode, with the source table scoped to each call.
    for _, name in ipairs({"UpdateClearBreadcrumbButtonHelpers",
                           "ClearTabBreadcrumb"}) do
        if targetLoot[name] == nil then
            local original = sourceLoot[name]
            if type(original) ~= "function" then
                error("original LOOT breadcrumb helper missing: " .. name)
            end
            local loggedUnavailable = false
            rawset(targetLoot, name, function(...)
                local previous = LOOT
                LOOT = sourceLoot
                local ok, result = pcall(original, ...)
                LOOT = previous
                if ok then return result end
                if not loggedUnavailable then
                    loggedUnavailable = true
                    ReplayOnline.Log("1.64.5 " .. name ..
                        " unavailable on TU19: " .. tostring(result))
                end
                return nil
            end)
            addedLootBreadcrumbMethods[#addedLootBreadcrumbMethods + 1] = name
        end
    end
end

-- Trace the original title's logo binding from the moment its source modules
-- compile. If the bytecode captures RegisterMaterial during registration, a
-- wrapper added afterward would miss its later UpdateStaticState request.
local logoMaterialNames = {
    logo_cod_mw = true, logo_cod_store = true, logo_cod_wz = true,
    magma_ui1645_logo_cod_mw = true,
    magma_ui1645_logo_cod_store = true,
    magma_ui1645_logo_cod_wz = true
}
local originalRegisterMaterial = RegisterMaterial
-- These original images have unique names so TU19's same-named artwork
-- cannot win DB lookup. Inline pixels and streamed decoded bytes are unchanged.
local widgetImageAliases = {}
for _, name in ipairs({
    "cdl_logo_small", "ui_mp_wz_tab_highlight", "icon_lock",
    "button_gradient_active_top", "widg_gradient_bottom_to_top",
    "widg_gradient_center_out", "dpad_circle_segment", "dpad_circle_whole",
    "logo_cod_mw_sm", "logo_warzone_sm", "widg_gradient_left_to_right",
    "widg_gradient_right_to_left", "icon_new_pulse", "ui_changed_indicator",
    "hud_icon_overtime", "mw_snipe_sm", "v_ui_asset_vanguard_logo_sm",
    "ui_mp_wz_bkg_button_1", "ui_mp_wz_sep_menu_title", "ui_mp_wz_bkg_tabmenu",
    "button_ffa", "button_offline", "button_private", "button_tournament",
    "br_wz_s4_event_tab_logo", "ui_seasonal_event_hvv_tab", "ui_waveform_active",
    "ui_waveform_idle", "loot_card_overlay_tracer_white", "customization_empty_slot",
    "ui_emblem_randomize", "ui_emblem_randomize_favorites",
    "ui_emblem_randomize_favorite_operator", "ui_playercard_randomize",
    "ui_playercard_randomize_favorites", "icon_xp", "battlepass_season_emblem_gold",
    "battlepass_season_emblem_silver", "mm_blade_store_warzone",
    "mm_blade_battlepass_warzone", "corners", "icon_xp_token", "button_glow_circle",
    "button_glow", "frontend_main_menu", "stencil_mask", "ui_generic_glow",
    "icon_playlist_players", "widg_gradient_top_to_bottom", "placeholder_x", "icon_new_store_pulse",
    "menu_backer_redact_white", "vanguard_owner_xp_boost_icon_0", "vanguard_owner_xp_boost_icon_1",
    "playstation_xp_boost_icon_0", "playstation_xp_boost_icon_1",
    "cortez_store_upsell_vault_instant_grat", "cortez_store_upsell_vault_instant_grat_three_blade",
    "new_game_reveal_button_background", "cortez_free_trial_button_background",
    "wdg_selection_arrow_right_1", "ui_mp_wz_bkg_button_2", "ui_mp_wz_bkg_button_3",
    "ui_mp_br_shared_button_loadout_background_ch2", "button_tdm", "button_rebirth_inactive",
    "icon_double_xp_mini", "icon_double_weapon_xp_mini", "icon_double_battle_xp_mini",
    "icon_double_operator_xp_mini", "icon_xp_token_unavailable",
    "ui_mp_wz_bkg_button_4", "icon_double_buff_xp_mini",
    "ui_t9_shared_icon_logos_challenges_combat", "ui_t9_shared_icon_logos_challenges_dailytierskips"
}) do widgetImageAliases[name] = "magma_ui1645_" .. name end
local materialProbeActive = false
local logoMaterialRequests = {}
RegisterMaterial = function(name, ...)
    if logoMaterialNames[name] and not materialProbeActive then
        logoMaterialRequests[name] = (logoMaterialRequests[name] or 0) + 1
        ReplayOnline.Log("1.64.5 runtime logo material request " ..
            tostring(name) .. " count=" .. logoMaterialRequests[name])
    end
    return originalRegisterMaterial(widgetImageAliases[name] or name, ...)
end


-- The original 1.64.5 MP menu refreshes attachment images when it opens.
-- Replay TU19 has the same profile tree and native Streaming add/remove APIs,
-- but its earlier dynamic utility has no Reload method or later connection
-- predicate. Execute the original source method only after the target profile
-- tree exists, with a bounded wait; preserve the native target utility table.
local addedDynamicAttachmentReload = false
local function importOriginalDynamicAttachmentReload()
    local chunk = ReplayMenu1645.Compile("ui/utils/dynamicattachmentutils.lua")
    local targetDyn = DYN_ATTACHMENT
    if type(chunk) ~= "function" or type(targetDyn) ~= "table" or
        type(PlayerData) ~= "table" or type(LOADOUT) ~= "table" or
        type(Streaming) ~= "table" then
        error("original dynamic-attachment utility or TU19 state is unavailable")
    end
    DYN_ATTACHMENT = setmetatable({}, {__index = targetDyn})
    local loaded, loadError = pcall(chunk,
        "ui.utils.dynamicattachmentutils_magma1645")
    local sourceDyn = DYN_ATTACHMENT
    DYN_ATTACHMENT = targetDyn
    if not loaded or type(sourceDyn) ~= "table" or
        type(sourceDyn.ReloadLoadoutWeaponStreamedImages) ~= "function" or
        type(sourceDyn.UpdateLoadoutWeaponStreamedImages) ~= "function" or
        type(sourceDyn.ProcessLoadoutWeaponStreamedImages) ~= "function" then
        error("original 1.64.5 attachment-image utility failed: " ..
            tostring(loadError))
    end
    if targetDyn.ReloadLoadoutWeaponStreamedImages ~= nil then return end
    local original = sourceDyn.ReloadLoadoutWeaponStreamedImages
    rawset(targetDyn, "ReloadLoadoutWeaponStreamedImages",
        function(menu, controller)
            local attempts = 0
            local function profileReady()
                local ok, count = pcall(function()
                    local group = LOADOUT.GetLoadoutStatsGroup()
                    local profile = PlayerData.BFFBGAJGD(controller, group)
                    local loadouts = profile.squadMembers.loadouts
                    local defaults = LOADOUT.GetDefaultLoadouts(controller)
                    if type(defaults) ~= "table" then return nil end
                    return #loadouts
                end)
                return ok and type(count) == "number"
            end
            local function perform()
                -- Source Update checks Engine.EEGHCCDG before the ADD pass.
                -- That later native symbol is absent from TU19; the profile
                -- check above supplies the relevant target-side readiness.
                local previousDyn = DYN_ATTACHMENT
                local previousConnection = rawget(Engine, "EEGHCCDG")
                DYN_ATTACHMENT = sourceDyn
                if previousConnection == nil then
                    rawset(Engine, "EEGHCCDG", function() return true end)
                end
                local ok, result = pcall(original, menu, controller)
                if previousConnection == nil then
                    rawset(Engine, "EEGHCCDG", nil)
                end
                DYN_ATTACHMENT = previousDyn
                if not ok then
                    ReplayOnline.Log("1.64.5 attachment-image refresh " ..
                        "unavailable on TU19: " .. tostring(result))
                end
            end
            local function tryWhenReady()
                if profileReady() then return perform() end
                attempts = attempts + 1
                if attempts <= 20 and menu and type(menu.Wait) == "function" then
                    local wait = menu:Wait(100)
                    if wait then
                        wait.onComplete = tryWhenReady
                        return
                    end
                end
                ReplayOnline.Log("1.64.5 attachment-image refresh skipped: " ..
                    "TU19 loadout profile unavailable after " .. attempts ..
                    " checks")
            end
            tryWhenReady()
        end)
    addedDynamicAttachmentReload = true
end


-- The later MP menu tests VIP-gift availability on entry. Replay's earlier
-- battle-pass utility lacks this method. Load only the exact source method;
-- preserve the target utility for all existing functions and treat missing
-- later commerce services as an unavailable offer, not an invented reward.
local addedVIPGiftAvailable = false
local function importOriginalVIPGiftAvailability()
    local chunk = ReplayMenu1645.Compile("ui/utils/battlepassutils.lua")
    local targetPass = BATTLEPASS
    if type(chunk) ~= "function" or type(targetPass) ~= "table" then
        error("original battle-pass utility or TU19 BATTLEPASS is unavailable")
    end
    BATTLEPASS = setmetatable({}, {__index = targetPass})
    local loaded, loadError = pcall(chunk,
        "ui.utils.battlepassutils_magma1645")
    local sourcePass = BATTLEPASS
    BATTLEPASS = targetPass
    if not loaded or type(sourcePass) ~= "table" or
        type(sourcePass.VIPGiftAvailable) ~= "function" then
        error("original 1.64.5 VIP-gift utility failed: " ..
            tostring(loadError))
    end
    if targetPass.VIPGiftAvailable ~= nil then return end
    local original = sourcePass.VIPGiftAvailable
    local loggedUnavailable = false
    rawset(targetPass, "VIPGiftAvailable", function(controller)
        local ok, available = pcall(original, controller)
        if ok then return available end
        if not loggedUnavailable then
            loggedUnavailable = true
            ReplayOnline.Log("1.64.5 VIP-gift availability unavailable " ..
                "on TU19: " .. tostring(available))
        end
        return false
    end)
    addedVIPGiftAvailable = true
end


-- The original Operator tab tests RANDOM.IsEnabled for both factions while
-- constructing its scene. TU19 has no randomutils LuaFile; the original gate
-- returns false when its later feature dvar is disabled. Preserve any target
-- RANDOM table and publish only source methods missing from it.
local previousRandom = RANDOM
local addedRandomTable = false
local addedRandomFields = {}
local function importOriginalRandomUtility()
    local chunk = ReplayMenu1645.Compile("ui/utils/randomutils.lua")
    if type(chunk) ~= "function" then
        error("original 1.64.5 operator randomization utility is missing")
    end
    local targetRandom = RANDOM
    RANDOM = {}
    local loaded, loadError = pcall(chunk, "ui.utils.randomutils_magma1645")
    local sourceRandom = RANDOM
    RANDOM = targetRandom
    if not loaded or type(sourceRandom) ~= "table" or
        type(sourceRandom.IsEnabled) ~= "function" or
        type(sourceRandom.GetItemData) ~= "function" then
        error("original 1.64.5 operator randomization failed: " ..
            tostring(loadError))
    end
    if type(targetRandom) ~= "table" then
        RANDOM = sourceRandom
        addedRandomTable = true
    else
        for _, name in ipairs({"IsEnabled", "GetItemData", "GetPlayerData"}) do
            if targetRandom[name] == nil and type(sourceRandom[name]) == "function" then
                rawset(targetRandom, name, sourceRandom[name])
                addedRandomFields[#addedRandomFields + 1] = name
            end
        end
    end
end

local modules = {
    -- Utilities used by the title and Warzone blades.
    {"ui/utils/mainmenuutils.lua", "ui.utils.MainMenuUtils"},
    {"ui/utils/wzmainmenuutils.lua", "ui.utils.WZMainMenuUtils"},
    {"ui/utils/giftingutils.lua", "ui.utils.GiftingUtils"},
    {"ui/utils/mp/restrictionsutils.lua", "ui.utils.mp.RestrictionsUtils"},
    {"ui/utils/eventrewardsutils.lua", "ui.utils.EventRewardsUtils"},

    -- Newly registered 1.64.5 title blade children.
    {"ui/frontend/crmliveeventbladeinfo.lua", "ui.frontEnd.CRMLiveEventBladeInfo"},
    {"ui/frontend/installstatus.lua", "ui.frontEnd.InstallStatus"},
    {"ui/frontend/mainmenubladelist.lua", "ui.frontEnd.MainMenuBladeList"},
    {"ui/frontend/mainmenubladengoverlay.lua", "ui.frontEnd.MainMenuBladeNGOverlay"},
    {"ui/frontend/mainmenubladestoreoverlay.lua", "ui.frontEnd.MainMenuBladeStoreOverlay"},
    {"ui/frontend/newgamerevealbanner.lua", "ui.frontEnd.NewGameRevealBanner"},
    {"ui/frontend/preorderbanner.lua", "ui.frontEnd.PreOrderBanner"},
    {"ui/frontend/vanguardrevealliveevent.lua", "ui.frontEnd.VanguardRevealLiveEvent"},
    {"ui/frontend/wzbladebutton.lua", "ui.frontEnd.WZBladeButton"},
    {"ui/frontend/wzbladesfriendcount.lua", "ui.frontEnd.WZBladesFriendCount"},
    {"ui/frontend/mp/wzbladesnewnotif.lua", "ui.frontEnd.mp.WZBladesNewNotif"},

    -- Shared original title widgets, then their parent builders.
    {"ui/frontend/mainmenubladedisableoverlay.lua", "ui.frontEnd.MainMenuBladeDisableOverlay"},
    {"ui/frontend/mainmenubladelayout.lua", "ui.frontEnd.MainMenuBladeLayout"},
    {"ui/frontend/mainmenubladenewbanner.lua", "ui.frontEnd.MainMenuBladeNewBanner"},
    {"ui/frontend/mainmenubladepromptbutton.lua", "ui.frontEnd.MainMenuBladePromptButton"},
    {"ui/frontend/mainmenubladeupselloverlay.lua", "ui.frontEnd.MainMenuBladeUpsellOverlay"},
    {"ui/frontend/mainmenubutton.lua", "ui.frontEnd.MainMenuButton"},
    {"ui/frontend/mainmenugrid.lua", "ui.frontEnd.MainMenuGrid"},
    {"ui/frontend/mainmenulogo.lua", "ui.frontEnd.MainMenuLogo"},
    {"ui/frontend/wzmainmenubladelayout.lua", "ui.frontEnd.WZMainMenuBladeLayout"},
    {"ui/frontend/mainmenublade.lua", "ui.frontEnd.MainMenuBlade"},
    {"ui/frontend/wzmainmenublade.lua", "ui.frontEnd.WZMainMenuBlade"},
    {"ui/frontend/mainmenuoffline.lua", "ui.frontEnd.MainMenuOffline"},
    {"ui/frontend/mainmenu.lua", "ui.frontEnd.MainMenu"},
    {"ui/frontend/wzmainmenu.lua", "ui.frontEnd.WZMainMenu"},

    -- Original MP navigation and reachable original MP children.
    {"ui/widgets/tabbacker.lua", "ui.widgets.TabBacker"},
    {"ui/widgets/menutitle.lua", "ui.widgets.MenuTitle"},
    {"ui/widgets/submenutab.lua", "ui.widgets.SubMenuTab"},
    {"ui/widgets/cdlrestrictiontag.lua", "ui.widgets.CDLRestrictionTag"},
    {"ui/widgets/mpplayerdetails.lua", "ui.widgets.MPPlayerDetails"},
    {"ui/widgets/buttonglow.lua", "ui.widgets.ButtonGlow"},
    {"ui/widgets/socialplayerbuttonbackground.lua", "ui.widgets.SocialPlayerButtonBackground"},
    {"ui/frontend/mp/playercount.lua", "ui.frontEnd.mp.PlayerCount"},
    {"ui/frontend/mp/friendsbutton.lua", "ui.frontEnd.mp.FriendsButton"},
    {"ui/frontend/socialpartywidgettalkers.lua", "ui.frontEnd.SocialPartyWidgetTalkers"},
    {"ui/frontend/socialpartywidgetlayout.lua", "ui.frontEnd.SocialPartyWidgetLayout"},
    {"ui/frontend/socialpartywidget.lua", "ui.frontEnd.SocialPartyWidget"},
    {"ui/widgets/wzaccentmenuoverlay.lua", "ui.widgets.WZAccentMenuOverlay"},
    {"ui/widgets/emptyfocusable.lua", "ui.widgets.EmptyFocusable"},
    {"ui/widgets/dailymodifiercountdown.lua", "ui.widgets.DailyModifierCountdown"},
    {"ui/widgets/xpboostelement.lua", "ui.widgets.XPBoostElement"},
    {"ui/widgets/xpboost.lua", "ui.widgets.XPBoost"},
    {"ui/widgets/widedoublexpnotifications.lua", "ui.widgets.WideDoubleXPNotifications"},
    {"ui/widgets/aarcontentheader.lua", "ui.widgets.AARContentHeader"},
    {"ui/widgets/doublenotchedbacker.lua", "ui.widgets.DoubleNotchedBacker"},
    {"ui/widgets/challengeresettimer.lua", "ui.widgets.ChallengeResetTimer"},
    {"ui/widgets/mpchallengeprogressbar.lua", "ui.widgets.MPChallengeProgressBar"},
    {"ui/widgets/mp/wzchallengessummaryitem.lua", "ui.widgets.mp.WZChallengesSummaryItem"},
    {"ui/frontend/mp/wzchallengessummaryrewardspanel.lua", "ui.frontEnd.mp.WZChallengesSummaryRewardsPanel"},
    {"ui/widgets/challengessummary.lua", "ui.widgets.ChallengesSummary"},
    {"ui/frontend/mp/playlistfeaturedcoundowntimer.lua", "ui.frontEnd.mp.PlaylistFeaturedCoundownTimer"},
    {"ui/frontend/mp/newgamerevealcountdowntimer.lua", "ui.frontEnd.mp.NewGameRevealCountdownTimer"},
    {"ui/frontend/mp/instantgratpopup.lua", "ui.frontEnd.mp.InstantGratPopup"},
    {"ui/frontend/mp/playlistbuttonsmall.lua", "ui.frontEnd.mp.PlaylistButtonSmall"},
    {"ui/frontend/mp/brplaylistbutton.lua", "ui.frontEnd.mp.BRPlaylistButton"},
    {"ui/frontend/playlistbuttonslim.lua", "ui.frontEnd.PlaylistButtonSlim"},
    {"ui/widgets/doublexpnotificationelement.lua", "ui.widgets.DoubleXPNotificationElement"},
    {"ui/widgets/quickplaybuttonbackground.lua", "ui.widgets.QuickPlayButtonBackground"},
    {"ui/widgets/sprite.lua", "ui.widgets.Sprite"},
    {"ui/widgets/loadoutitembuttonbackground.lua", "ui.widgets.LoadoutItemButtonBackground"},
    {"ui/widgets/buttonhighlightbar.lua", "ui.widgets.ButtonHighlightBar"},
    {"ui/frontend/crmweb.lua", "ui.frontEnd.CRMWeb"},
    {"ui/frontend/eventpromobanner.lua", "ui.frontEnd.EventPromoBanner"},
    {"ui/frontend/mp/trialeventaccordionbutton.lua", "ui.frontEnd.mp.TrialEventAccordionButton"},
    {"ui/frontend/mp/wzlookforpartysearchstatustopper.lua", "ui.frontEnd.mp.WZLookForPartySearchStatusTopper"},
    {"ui/widgets/mp/blueprintblitzmainmenuitem.lua", "ui.widgets.mp.BlueprintBlitzMainMenuItem"},
    {"ui/frontend/mp/brgamemodeplaybutton.lua", "ui.frontEnd.mp.BRGameModePlayButton"},
    {"ui/frontend/mp/brplaybutton.lua", "ui.frontEnd.mp.BRPlayButton"},
    {"ui/frontend/mp/brpromotedbutton.lua", "ui.frontEnd.mp.BRPromotedButton"},
    {"ui/frontend/mp/brfeaturebutton.lua", "ui.frontEnd.mp.BRFeatureButton"},
    {"ui/frontend/mp/brtrialsplaybutton.lua", "ui.frontEnd.mp.BRTrialsPlayButton"},
    {"ui/frontend/mp/brplaymenupromotedheader.lua", "ui.frontEnd.mp.BRPlayMenuPromotedHeader"},
    {"ui/frontend/mp/brplaymenupromotedbuttons.lua", "ui.frontEnd.mp.BRPlayMenuPromotedButtons"},
    {"ui/frontend/mp/brplaymenusquadbuttons.lua", "ui.frontEnd.mp.BRPlayMenuSquadButtons"},
    {"ui/frontend/mp/brplaymenubuttons.lua", "ui.frontEnd.mp.BRPlayMenuButtons"},
    {"ui/frontend/mp/brplaymenu.lua", "ui.frontEnd.mp.BRPlayMenu"},
    {"ui/frontend/mp/mpplaymenubuttons.lua", "ui.frontEnd.mp.MPPlayMenuButtons"},
    {"ui/frontend/mp/mpplaymenu.lua", "ui.frontEnd.mp.MPPlayMenu"},
    {"ui/frontend/mpoperatormenubuttons.lua", "ui.frontEnd.MPOperatorMenuButtons"},
    {"ui/frontend/mpoperatormenu.lua", "ui.frontEnd.MPOperatorMenu"},
    {"ui/frontend/mpweaponmenubuttons.lua", "ui.frontEnd.MPWeaponMenuButtons"},
    {"ui/frontend/mpweaponmenu.lua", "ui.frontEnd.MPWeaponMenu"},
    {"ui/frontend/mp/brmainmenu.lua", "ui.frontEnd.mp.BRMainMenu"},
    {"ui/frontend/mp/mpmainmenu.lua", "ui.frontEnd.mp.MPMainMenu"},
    {"ui/frontend/mptabsmenu.lua", "ui.frontEnd.MPTabsMenu"},
}

local previous = {}
for name, constructor in pairs(MenuBuilder.m_types) do previous[name] = constructor end
-- TU19 keeps its equivalent Battle Pass selected-item/prompt refresh private.
-- Verified native constructor path: postload -> unlock animator -> refresh.
-- Keep its native closure, including the three original prompt upvalues.
local targetBattlePassRefresh = previous.battlepassmenu
if type(debug) == "table" and type(debug.getupvalue) == "function" then
    for _, index in ipairs({1, 3, 1}) do
        if type(targetBattlePassRefresh) ~= "function" then break end
        local _, value = debug.getupvalue(targetBattlePassRefresh, index)
        targetBattlePassRefresh = value
    end
else
    targetBattlePassRefresh = nil
end
local previousGift = GIFT
local main = MAIN_MENU
local oldMain = {}
for name, value in pairs(main) do oldMain[name] = value end
local warzoneMain = WZ_MAIN_MENU
local oldWarzoneMain = {}
if type(warzoneMain) == "table" then
    for name, value in pairs(warzoneMain) do oldWarzoneMain[name] = value end
end
local oldLoaded = {}
for _, entry in ipairs(modules) do
    -- TU19's already-loaded module tables reject later-era module variables.
    -- Keep every original LuaFile's bytecode intact but give its package
    -- namespace a distinct leaf; builders remain registered by native name.
    local namespace = entry[2] .. "_magma1645"
    local value = package.loaded[namespace]
    oldLoaded[namespace] = {exists = value ~= nil, value = value}
end

local originalRegister = MenuBuilder.registerType
local originalFences = LUI.FlowManager.RegisterFenceGroup
MenuBuilder.registerType = function(name, constructor, ...)
    if type(name) == "string" and type(constructor) == "function" then
        local key = string.lower(name)
        if MenuBuilder.m_types[key] then
            MenuBuilder.m_types[key] = constructor
            return
        end
    end
    return originalRegister(name, constructor, ...)
end
LUI.FlowManager.RegisterFenceGroup = function(name, ...)
    -- These 1.64.5 groups include later services (catalog, frontendAssets,
    -- mpPrivilegeF2P) that TU19 does not register. Keep its already-loaded
    -- target fence groups for the imported title, MP and Warzone builders.
    if name == "MainMenu" or name == "MPMainMenu" or
        name == "BRMainMenu" or name == "BRMainMenuStore" then return end
    return originalFences(name, ...)
end

local currentModule = "(none)"
local ok, errorText = pcall(function()
    currentModule = "ui/utils/timeutils.lua"
    importOriginalTime()
    currentModule = "ui/uieditor/colors.lua + swatches.lua"
    importOriginalSwatches()
    currentModule = "ui/uieditor/swatchthemes.lua + ui/utils/themeutil.lua"
    importOriginalThemes()
    currentModule = "ui/utils/layoututils.lua"
    importOriginalLayout()
    currentModule = "ui/utils/progressionutils.lua"
    importOriginalCustomLoadoutPredicate()
    currentModule = "ui/utils/storeutils.lua"
    importOriginalStoreSaleTimer()
    currentModule = "ui/utils/trialsutils.lua"
    importOriginalTrialsFixup()
    currentModule = "ui/utils/lootutils.lua + operatorutils.lua"
    importOriginalCustomizationChecks()
    currentModule = "ui/utils/dynamicattachmentutils.lua"
    importOriginalDynamicAttachmentReload()
    currentModule = "ui/utils/battlepassutils.lua"
    importOriginalVIPGiftAvailability()
    currentModule = "ui/utils/randomutils.lua"
    importOriginalRandomUtility()
    for _, entry in ipairs(modules) do
        currentModule = entry[1]
        local chunk = ReplayMenu1645.Compile(entry[1])
        if type(chunk) ~= "function" then error("Missing original 1.64.5 module: " .. entry[1]) end
        chunk(entry[2] .. "_magma1645")
    end
    if type(GIFT) ~= "table" or type(GIFT.ProcessQueuedBundleGifts) ~= "function" then
        error("Original 1.64.5 gifting queue utility did not register")
    end
end)

MenuBuilder.registerType = originalRegister
LUI.FlowManager.RegisterFenceGroup = originalFences
if not ok then
    if addedThemesGlobal then THEMES = previousThemes end
    if type(previousThemes) == "table" then
        for i = #addedThemeFields, 1, -1 do
            rawset(previousThemes, addedThemeFields[i], nil)
        end
    end
    for i = #addedTimeFields, 1, -1 do rawset(TIME, addedTimeFields[i], nil) end
    for i = #addedLayoutFields, 1, -1 do
        rawset(LAYOUT, addedLayoutFields[i], nil)
    end
    if addedCustomLoadoutPredicate then
        rawset(PROGRESSION, "AreCustomLoadoutsUnlocked", nil)
    end
    if addedStoreSaleTimer then rawset(STORE, "GetSaleTimer", nil) end
    if addedVanguardAvailability then rawset(STORE, "IsVanguardAvailable", nil) end
    if addedTrialsFixup then rawset(TRIALS, "FixUpLoot", nil) end
    if addedLootCustomizationCheck then
        rawset(LOOT, "CheckCustomizationsShouldBeEquipped", nil)
    end
    for i = #addedLootBreadcrumbMethods, 1, -1 do
        rawset(LOOT, addedLootBreadcrumbMethods[i], nil)
    end
    if addedOperatorCustomizationCheck then
        rawset(OPERATOR, "CheckCustomizationsShouldBeEquipped", nil)
    end
    if addedWeaponOwnershipCheck then
        rawset(WEAPON, "CheckWeaponsShouldBeEquipped", nil)
    end
    if addedDynamicAttachmentReload then
        rawset(DYN_ATTACHMENT, "ReloadLoadoutWeaponStreamedImages", nil)
    end
    if addedVIPGiftAvailable then
        rawset(BATTLEPASS, "VIPGiftAvailable", nil)
    end
    if addedRandomTable then RANDOM = previousRandom end
    if type(previousRandom) == "table" then
        for i = #addedRandomFields, 1, -1 do
            rawset(previousRandom, addedRandomFields[i], nil)
        end
    end
    if addedGameSourceID then
        rawset(LUI, "GAME_SOURCE_ID", nil)
    elseif type(LUI.GAME_SOURCE_ID) == "table" then
        for i = #addedGameSourceIDFields, 1, -1 do
            rawset(LUI.GAME_SOURCE_ID, addedGameSourceIDFields[i], nil)
        end
    end
    for i = #addedSwatchFields, 1, -1 do
        rawset(addedSwatchFields[i][1], addedSwatchFields[i][2], nil)
    end
    for i = #addedSwatchGroups, 1, -1 do
        rawset(SWATCHES, addedSwatchGroups[i], nil)
    end
    for _, name in ipairs(addedConditions) do targetConditions[name] = nil end
    for _, name in ipairs(addedCrmLocations) do CRM.locations[name] = nil end
    if addedCursorMethod then rawset(LUI.UIMouseCursor, "ForceCursorVisibility", nil) end
    for _, name in ipairs(addedElementMethods) do rawset(LUI.UIElement, name, nil) end
    for i = #addedTabManagerFields, 1, -1 do
        local field = addedTabManagerFields[i]
        rawset(LUI.TabManager, field[1], field[2])
    end
    if addedLobbyMusic then rawset(DataSources.frontEnd.lobby, "lobbyMusic", nil) end
    if addedDelayedCharacters then rawset(Lobby, "UpdateClientCharactersDelayed", nil) end
    GIFT = previousGift
    if addedWebViewManager then rawset(LUI, "WebViewManager", previousWebViewManager) end
    for _, name in ipairs(addedSeasonalHelpers) do rawset(CoD, name, nil) end
    if addedTitleNativeHook then rawset(Engine, "DDEJECEIBB", nil) end
    for _, name in ipairs(addedPopupNativeChecks) do rawset(Engine, name, nil) end
    if addedSeasonNumber then rawset(SEASON, "GetCurrentSeasonDataNum", nil) end
    if addedSeasonDisplayNumber then rawset(SEASON, "GetCurrentSeasonDisplayNum", nil) end
    if addedMidSeasonVideo then rawset(Onboarding, "MidSeasonVideo", nil) end
    if addedReverseNukeVideo then rawset(Onboarding, "ReverseNukeVideo", nil) end
    if addedBRDefaultMap then rawset(BR, "DEFAULT_MAP", nil) end
    for _, name in ipairs(addedWzwip) do rawset(wzwip, name, nil) end
    if type(previousWzwip) ~= "table" then rawset(_G, "WZWIP", previousWzwip) end
    for name in pairs(MenuBuilder.m_types) do
        if previous[name] == nil then MenuBuilder.m_types[name] = nil end
    end
    for name, constructor in pairs(previous) do MenuBuilder.m_types[name] = constructor end
    for name in pairs(main) do
        if oldMain[name] == nil then main[name] = nil end
    end
    for name, value in pairs(oldMain) do main[name] = value end
    if type(warzoneMain) == "table" then
        for name in pairs(warzoneMain) do
            if oldWarzoneMain[name] == nil then warzoneMain[name] = nil end
        end
        for name, value in pairs(oldWarzoneMain) do warzoneMain[name] = value end
    else
        WZ_MAIN_MENU = nil
    end
    for name, state in pairs(oldLoaded) do
        package.loaded[name] = state.exists and state.value or nil
    end
    RegisterMaterial = originalRegisterMaterial
    ReplayOnline.Log("original 1.64.5 menu registration failed in " .. currentModule .. ": " .. tostring(errorText))
    return
end

rawset(MenuBuilder, "_replayMenu1645Active", true)
ReplayOnline.Log("original 1.64.5 title and MP menu builders registered")

-- BEGIN native Warzone menu entry bridge.
-- Both original constructors capture postload at upvalue 2, then their
-- menu_create callback at upvalue 2. Keep the later widget tree and navigation,
-- but use TU19's actual lobby entry/configuration/onboarding/CRM callback.
-- The later callback additionally assumes cross-title gifting, web MOTD and
-- profile fields absent from this runtime. Do not fabricate those services.
do
    local nativeBuild = previous.brmainmenu
    local sourceBuild = MenuBuilder.m_types.brmainmenu
    if type(nativeBuild) == "function" and type(sourceBuild) == "function" and
        type(debug.getupvalue) == "function" and type(debug.setupvalue) == "function" then
        local _, nativePostload = debug.getupvalue(nativeBuild, 2)
        local _, sourcePostload = debug.getupvalue(sourceBuild, 2)
        assert(type(nativePostload) == "function" and type(sourcePostload) == "function",
            "Warzone postload binding changed")
        local _, nativeCreate = debug.getupvalue(nativePostload, 2)
        local _, sourceCreate = debug.getupvalue(sourcePostload, 2)
        assert(type(nativeCreate) == "function" and type(sourceCreate) == "function",
            "Warzone menu_create binding changed")
        debug.setupvalue(sourcePostload, 2, nativeCreate)
        ReplayOnline.Log("original Warzone UI bound to TU19 lobby entry callback")
    end
end
-- END native Warzone menu entry bridge.

-- BEGIN original tab helper bridge.
-- TU19 TAB.SetUp installs its own Setup callback and expects text/image
-- arguments. The later SubMenuTab has already installed Setup and calls the
-- helper without those arguments; TU19 therefore erased its label setter.
-- Use the original later helper for that widget only. Other target tabs keep
-- the native helper and its layout/navigation contract.
do
    local nativeTab = TAB
    local originalTabChunk = ReplayMenu1645.Compile("ui/utils/tabutils.lua")
    local sourceTab = nil
    if type(originalTabChunk) == "function" and type(nativeTab) == "table" then
        TAB = {}
        local loaded, problem = pcall(originalTabChunk, "ui.utils.tabutils_magma1645")
        sourceTab = TAB
        TAB = nativeTab
        if loaded and type(sourceTab.SetUp) == "function" and
            type(sourceTab.SetTabState) == "function" then
            local nativeSetup = nativeTab.SetUp
            rawset(nativeTab, "TAB_EVENT", sourceTab.TAB_EVENT)
            rawset(nativeTab, "SetTabState", sourceTab.SetTabState)
            rawset(nativeTab, "SetUp", function(widget, ...)
                if widget and widget.id == "SubMenuTab" and widget.TabText and
                    type(widget.Setup) == "function" and select("#", ...) == 0 then
                    return sourceTab.SetUp(widget, ...)
                end
                return nativeSetup(widget, ...)
            end)
        else
            ReplayOnline.Log("original tab helper unavailable: " .. tostring(problem))
        end
    end
end
-- END original tab helper bridge.

-- Retain TU19's Battle Pass catalog and progression. The later parent asks
-- for a public refresh after attachment; use the target's original callback.
if type(previous.battlepassmenu) == "function" and type(targetBattlePassRefresh) == "function" then
    local buildBattlePass = previous.battlepassmenu
    MenuBuilder.m_types.battlepassmenu = function(...)
        local widget = buildBattlePass(...)
        if widget and widget.UpdateMenuDisplay == nil then
            widget.UpdateMenuDisplay = targetBattlePassRefresh
        end
        return widget
    end
    ReplayOnline.Log("1.64.5 parent bound to original TU19 Battle Pass refresh")
else
    ReplayOnline.Log("TU19 Battle Pass refresh binding unavailable")
end

-- TU19 has no later Look For Party service or active search. Hide that
-- optional indicator while ordinary native party/matchmaking status remains.
if type(Lobby) == "table" and type(Lobby.DGDCDCHFGD) ~= "function" then
    rawset(Lobby, "DGDCDCHFGD", function() return false end)
    MenuBuilder.m_types.wzlookforpartysearchstatustopper = function()
        local widget = LUI.UIElement.new()
        widget.id = "WZLookForPartySearchStatusTopper"
        widget:SetAlpha(0, 0)
        return widget
    end
    ReplayOnline.Log("1.64.5 Look For Party indicator disabled: service unavailable on TU19")
end

-- BEGIN TU19 widget bridges. These adapt source call signatures while
-- retaining the target's real squad-fill and character implementations.
-- BEGIN target playlist visibility bridge.
-- Later CFECCDIFGE reads bit 6 of a new category flags byte (source native
-- RVA 0x30BC100). TU19's BR menu displays every MAGMA/ALL category returned
-- by its public datasource; keep that published catalog policy. Never mark
-- an index absent from that native datasource visible.
if type(Playlist) == "table" and type(Playlist.CFECCDIFGE) ~= "function" then
    rawset(Playlist, "CFECCDIFGE", function(index)
        if type(index) ~= "number" then return false end
        local categories = PlaylistUtils.GetPublicPlaylistDatasources()
        for i = 0, categories:GetCountValue(0) - 1 do
            local category = categories:GetDataSourceAtIndex(i)
            if category.index:GetValue(0) == index then return true end
        end
        return false
    end)
end
if type(PlaylistUtils) == "table" and type(PlaylistUtils.AddDisableOtherButtonFocusOnButtons) ~= "function" then
    local chunk = ReplayMenu1645.Compile("ui/utils/playlistutils.lua")
    assert(type(chunk) == "function", "Original playlist focus helper missing")
    local env = setmetatable({PlaylistUtils = {}, MenuBuilder = {registerType = function() end}}, {__index = _G})
    setfenv(chunk, env)
    chunk()
    for _, name in ipairs({"AddDisableOtherButtonFocusOnButtons", "UpdateBackgroundImage",
        "UpdatePlaylistRestrictionVisualsViaDataSource", "isEarlyAccessLocked",
        "DoesPlaylistCategoryHaveRestrictions", "DoesPlaylistRequireRestrictionWarning",
        "TryShowBtnActionPlaylistRestrictionPopups", "IsPlaylistBtnRestrictedForUser",
        "GetRestrictedPlaylistIds", "GetActivePlaylistRestrictionTypeData", "IsPlaylistIdRestricted",
        "DoesPlaylistRequireSquadFill", "DoesPlaylistRequireScareWarning",
        "DoesPlaylistContainGameTypeInList", "GetCategoryGameTypes"}) do
        if PlaylistUtils[name] == nil then
            local callback = env.PlaylistUtils[name]
            assert(type(callback) == "function", "Original playlist visual helper missing: " .. name)
            rawset(PlaylistUtils, name, callback)
        end
    end
    for _, name in ipairs({"RestrictionTypes", "RestrictionTypeData"}) do
        if PlaylistUtils[name] == nil then
            assert(type(env.PlaylistUtils[name]) == "table", "Original restriction metadata missing: " .. name)
            rawset(PlaylistUtils, name, env.PlaylistUtils[name])
        end
    end
    -- These closures retain their original private PlaylistUtils table,
    -- including restriction metadata and helper dependencies. The source
    -- condition reads the real restriction dvar; an ordinary TU19 playlist
    -- has no later early-access flag. Do not invent either a restriction or
    -- ownership, or replace TU19's playlist selection/matchmaking helpers.
    for _, name in ipairs({"DEFAULT_RESTRICTION_HIDE_ANIM",
        "DEFAULT_RESTRICTION_ENABLED_ANIM", "DEFAULT_RESTRICTION_DISABLED_ANIM"}) do
        if PlaylistUtils[name] == nil then
            local value = env.PlaylistUtils[name]
            assert(type(value) == "string", "Original restriction sequence missing: " .. name)
            rawset(PlaylistUtils, name, value)
        end
    end
    -- Exact original layout constants used by BRPlayMenuButtons. Keep
    -- existing target values and catalog identifiers unchanged.
    for _, name in ipairs({"BR_GAME_MODE_BUTTON_WIDTH", "BUTTON_EXPAND_BOTTOM_SIZE",
        "BUTTON_COLLAPSE_BOTTOM_SIZE", "BUTTON_EXPAND_COLLAPSE_DURATION"}) do
        if PlaylistUtils[name] == nil then
            local value = env.PlaylistUtils[name]
            assert(type(value) == "number", "Original playlist layout constant missing: " .. name)
            rawset(PlaylistUtils, name, value)
        end
    end
end
-- END target playlist visibility bridge.
-- BEGIN original playlist sprite schema.
-- Exact luashared/csvutils.lua declaration; the original table graph is
-- included in the menu fastfile, with its packed source strings resolved.
if type(CSV) == "table" and CSV.playlistSpriteButtonData == nil then
    rawset(CSV, "playlistSpriteButtonData", {
        file = "mp/playlist_sprite_buttons.csv",
        cols = {id=0, ref=1, image=2, rows=3, columns=4,
            playbackRate=5, stopFrame=6, loop=7}
    })
end
-- END original playlist sprite schema.
-- BEGIN original playlist datasource namespace.
local nativeSetDataSourceDiagnostic = LUI.UIElement.SetDataSource
if type(nativeSetDataSourceDiagnostic) == "function" then
    LUI.UIElement.SetDataSource = function(self, row, controller)
        for link in pairs(self._dataLinksAsSource or {}) do
            local value, path = row, ""
            for _, part in ipairs(link._relativeDataSourcePath or {}) do
                path = path == "" and tostring(part) or path .. "." .. tostring(part)
                if value == nil then break end
                value = value[part]
            end
            if row ~= nil and value == nil then
                ReplayOnline.Log("1.64.5 model link missing source=" .. tostring(self.id) ..
                    " target=" .. tostring(link._target and link._target.id) ..
                    " path=" .. path .. " rowType=" .. type(row))
            end
        end
        return nativeSetDataSourceDiagnostic(self, row, controller)
    end
end
-- The original BR widgets subscribe to the relative editor path
-- DataSources.frontEnd.MP.playlist.desc as well as to desc. TU19's native
-- playlist row exposes desc directly. Both builds' DataLink.Refresh walk
-- each relative path segment; neither resolves a global DataSources here.
-- Supply that namespace only for the imported button, referencing the same
-- real row and model. Keep native widgets and published playlist data intact.
for _, builderName in ipairs({"brplaylistbutton", "brfeaturebutton"}) do
    local originalBRPlaylistButton = MenuBuilder.m_types[builderName]
    if type(originalBRPlaylistButton) == "function" then
    local nativeOrdinaryPlaylist = LUI.DataSourceInGlobalModel.new("frontEnd.magmaUI1645.ordinaryPlaylistLimitedTime")
    local absentOperatorBoost = LUI.DataSourceInGlobalModel.new("frontEnd.magmaUI1645.absentOperatorXPBoost")
    MenuBuilder.m_types[builderName] = function(...)
        local widget = originalBRPlaylistButton(...)
        local setDataSource = widget.SetDataSource
        widget.SetDataSource = function(self, dataSource, controller)
            if type(dataSource) == "table" then
                local row = dataSource
                local overlay = {}
                if row.DataSources == nil then overlay.DataSources={frontEnd={MP={playlist=row}}} end
                if row.isLimitedTime == nil then
                    -- The TU19 public catalog does not publish a later-era
                    -- limited-time badge. A false visual model hides it; it
                    -- does not alter availability, locks, or matchmaking.
                    nativeOrdinaryPlaylist:SetValue(controller, false)
                    overlay.isLimitedTime = nativeOrdinaryPlaylist
                end
                if row.operatorXpScale == nil then
                    -- TU19 has no operator progression; 1 means no bonus
                    -- icon in the original callback, not a granted balance.
                    absentOperatorBoost:SetValue(controller, 1)
                    overlay.operatorXpScale = absentOperatorBoost
                end
                dataSource = setmetatable(overlay, {__index=row})
            end
            return setDataSource(self, dataSource, controller)
        end
        return widget
    end
    end
end
-- END original playlist datasource namespace.
-- BEGIN native XP panel bridge.
-- TU19 RVA 0x1059870 takes one controller and returns four native expiry
-- timestamps (XP, weapon, key, Battle Pass). The later widget supplies a
-- title selector. Keep IW8 inventory/timers; absent cross-title and operator
-- progression must never inherit those balances or imply an entitlement.
if type(PROGRESSION) == "table" and type(OnlineProgression) == "table" and
    type(OnlineProgression.CEHEBBGFGG) == "function" then
    if type(PROGRESSION.GetTokenTitle) ~= "function" then
        rawset(PROGRESSION, "GetTokenTitle", function() return 0 end)
    end
    if type(PROGRESSION.GetSecondaryTokenTitle) ~= "function" then
        rawset(PROGRESSION, "GetSecondaryTokenTitle", function() return 1 end)
    end
    local getNativeTokenTimers = OnlineProgression.CEHEBBGFGG
    rawset(OnlineProgression, "CEHEBBGFGG", function(...)
        if select("#", ...) == 2 then
            local controller, title = ...
            if type(controller) == "number" then
                if title == 0 then return getNativeTokenTimers(controller) end
                if title == 1 then return nil, nil, nil, nil, nil end
            end
        end
        return getNativeTokenTimers(...)
    end)
end
if type(LOOT) == "table" then
    for name, field in pairs({GetDoubleXPTokens="DOUBLE_XP_TOKENS",
        GetDoubleWeaponXPTokens="DOUBLE_WEAPON_XP_TOKENS",
        GetDoubleKeyTokens="DOUBLE_KEY_TOKENS",
        GetDoubleBattlePassTokens="DOUBLE_BATTLE_PASS_TOKENS"}) do
        if type(LOOT[name]) ~= "function" and type(LOOT[field]) == "table" then
            local nativeField = field
            rawset(LOOT, name, function() return LOOT[nativeField] end)
        end
    end
    if type(LOOT.GetDoubleOperatorXPTokens) ~= "function" then
        rawset(LOOT, "GetDoubleOperatorXPTokens", function() return {} end)
    end
    if type(LOOT.doubleXpTypes) == "table" then
        if LOOT.doubleXpTypes.KEY == nil then rawset(LOOT.doubleXpTypes, "KEY", 4) end
        if LOOT.doubleXpTypes.OPERATOR == nil then rawset(LOOT.doubleXpTypes, "OPERATOR", 5) end
    end
    if type(LOOT.IsPSDoubleXPActive) ~= "function" then
        rawset(LOOT, "IsPSDoubleXPActive", function()
            return CONDITIONS.IsPS4() and Dvar.CFHDGABACF("MPPRMTPSLT") == 2
        end)
    end
end
-- END native XP panel bridge.
-- TU19 BRMainMenu plays music_mainmenu_mp directly. The imported parent
-- moved that argument into a new CoD field; keep the target Warzone track.
if type(CoD) == "table" and type(CoD.Music) == "table" and
    CoD.Music.MainWZMusic == nil then
    rawset(CoD.Music, "MainWZMusic", CoD.Music.MainMPMusic)
end
-- Native TU19 BRMainMenu publishes the PARTY presence context. The newer
-- PARTY_WZ enum is absent in its engine, so retain that original target value.
if type(CoD) == "table" and type(CoD.PresenceContext) == "table" and
    CoD.PresenceContext.PARTY_WZ == nil then
    rawset(CoD.PresenceContext, "PARTY_WZ", CoD.PresenceContext.PARTY)
end
-- Original seasonutils.lua implementation. It uses the existing target
-- season-number adapter and localization, rather than a hard-coded label.
if type(SEASON) == "table" and type(SEASON.GetLocalizedSimpleName) ~= "function" then
    rawset(SEASON, "GetLocalizedSimpleName", function(options)
        return Engine.CBBHFCGDIC("LUA_MENU/SEASON_NUM", SEASON.GetCurrentSeasonDisplayNum(options))
    end)
end
-- Source PlayerCount asks for counts with six arguments. TU19's native
-- GetMixedFriendsData at RVA 0x1AC7DD0 requires exactly five (three numbers,
-- two booleans). Only adapt the observed counts-only form; retain target
-- friend-list callers and return the native result without fabricating data.
if type(Friends) == "table" and type(Friends.FBFFEFEGB) == "function" then
    local getNativeFriends = Friends.FBFFEFEGB
    rawset(Friends, "FBFFEFEGB", function(...)
        if select("#", ...) == 6 then
            local controller, first, last, previousCount, includeEntries, includeOffline = ...
            if first == 0 and last == 0 and previousCount == 0 and
                includeEntries == false and includeOffline == false then
                return getNativeFriends(controller, 0, 0, false, false)
            end
        end
        return getNativeFriends(...)
    end)
end
if type(Lobby) == "table" and Lobby.lfpPlaylistBtnCount == nil then
    rawset(Lobby, "lfpPlaylistBtnCount", 0)
end
if type(Lobby) == "table" and type(Lobby.CreateFillToggleButton) ~= "function" and
    type(Lobby.AddFillToggleButton) == "function" then
    local addNativeFill = Lobby.AddFillToggleButton
    rawset(Lobby, "CreateFillToggleButton", function(parent, controller)
        -- TU19's helper creates AND attaches; the source caller attaches.
        -- Its only parent accesses are id, addElement and FillToggleButton.
        local collector = {id = parent.id}
        function collector:addElement(child) self.FillToggleButton = child end
        addNativeFill(collector, controller)
        local widget = assert(collector.FillToggleButton, "TU19 squad fill widget missing")
        widget:SetAnchorsAndPosition(0, 1, 0, 1, 0, 430 * _1080p,
            52 * _1080p, 106 * _1080p)
        return widget
    end)
end
if type(Lobby) == "table" and type(Lobby.CreateLookForPartyButton) ~= "function" then
    rawset(Lobby, "CreateLookForPartyButton", function(parent, controller)
        local widget = MenuBuilder.BuildRegisteredType("GenericButton", {controllerIndex = controller})
        widget.id = "LookForPartyButton" .. parent.id
        widget.Text:SetLeft(20 * _1080p, 0)
        widget.Text:setText(ToUpperCase(Engine.CBBHFCGDIC("LUA_MENU_MP/LOOK_FOR_PARTY")), 0)
        widget.Text:SetAlignment(LUI.Alignment.Left)
        widget:SetAnchorsAndPosition(0, 1, 0, 1, 0, 430 * _1080p,
            52 * _1080p, 106 * _1080p)
        -- This service is absent from the target. Keep its original place
        -- in the panel but never enable it on host/split-screen refresh.
        local setDisabled = widget.SetButtonDisabled
        widget.SetButtonDisabled = function(self) return setDisabled(self, true) end
        widget:SetButtonDisabled(true)
        return widget
    end)
end
if type(OPERATOR) == "table" and type(OPERATOR.UpdateClientCharacter) == "function" then
    local updateNativeCharacter = OPERATOR.UpdateClientCharacter
    rawset(OPERATOR, "UpdateClientCharacter", function(character, second, third, fourth, ...)
        if type(fourth) == "table" then
            -- Source: character, model, head, options. TU19's unused second
            -- parameter is controller; preserve the remaining ten-slot ABI.
            return updateNativeCharacter(character, nil, second, third,
                fourth.operatorPose, fourth.element, fourth.weaponModel,
                fourth.secondaryModel, fourth.showCharacter, fourth.accessoryModel)
        end
        return updateNativeCharacter(character, second, third, fourth, ...)
    end)
end
-- The new player-header widget uses a result table for data that TU19
-- exposes through separate getters. Keep the displayed rank, icon and XP
-- fraction from TU19; do not replace its seasonal progression system.
if type(PROGRESSION) == "table" and type(PROGRESSION.GetPlayerProgressionData) ~= "function" then
    rawset(PROGRESSION, "GetPlayerProgressionData", function(options)
        local controller = assert(options and options._controllerIndex,
            "TU19 header progression requires a local controller")
        local elder = PROGRESSION.IsElder(controller, true)
        local rank = PROGRESSION.GetPlayerRank(controller)
        local percentage = elder and PROGRESSION.GetSeasonRankPercentage(controller) or
            PROGRESSION.GetRankPercentage(controller)
        local color = SWATCHES.genericButton.description
        if PROGRESSION.IsPlayerLifeTimeXPMax(controller) then
            color = SWATCHES.Global.keyColor
        elseif elder then
            color = SWATCHES.MainMenu.IGRBonusText
        end
        return {
            _rank = rank, _rankDisplay = rank + 1,
            _rankIcon = PROGRESSION.GetPlayerRankIcon(controller),
            _rankPercentage = tonumber(percentage),
            _progressionColor = SWATCHES.genericMenu.progressFill,
            _prestigeMasterColor = color
        }
    end)
end
if type(PROGRESSION) == "table" and type(PROGRESSION.GetPlayerAtMaxRank) ~= "function" and
    type(PROGRESSION.IsPlayerLifeTimeRankMax) == "function" then
    rawset(PROGRESSION, "GetPlayerAtMaxRank", PROGRESSION.IsPlayerLifeTimeRankMax)
end
-- Source BR restore_focus reads event completion before testing event
-- availability. TU19 has no later seasonal-event progress schema. Report
-- unavailable completion, never synthesize earned rewards or write stats.
local targetSeasonal = SEASONAL_EVENT
if type(targetSeasonal) ~= "table" then
    targetSeasonal = {}
    rawset(_G, "SEASONAL_EVENT", targetSeasonal)
end
for _, name in ipairs({"AreAllSeasonalEventsCompleted", "IsSeasonalEventCompleteBinkSeen"}) do
    if type(targetSeasonal[name]) ~= "function" then
        rawset(targetSeasonal, name, function() return false end)
    end
end
-- END TU19 widget bridges.

-- The source MP tabs animate the PartyWidget's CDL badge on each tab change.
-- Replay builds its older SocialPartyWidget without those two sequences, so
-- the source-only badge animation must be skipped for that exact widget.
-- Leave every other animation call to the original action implementation.
if type(ACTIONS) == "table" and type(ACTIONS.AnimateSequence) == "function" then
    local animateSequence = ACTIONS.AnimateSequence
    local reportedMissingCdl = false
    local reportedOtherMissingSequence = false
    rawset(ACTIONS, "AnimateSequence", function(element, sequence, ...)
        if element and element.id == "PartyWidget" and
            (sequence == "CDLon" or sequence == "CDLoff") and
            type(element._sequences) ~= "table" then
            if not reportedMissingCdl then
                ReplayOnline.Log("1.64.5 PartyWidget CDL animation unavailable on TU19; using its static badge")
                reportedMissingCdl = true
            end
            return
        end
        if element and type(element._sequences) ~= "table" and
            not reportedOtherMissingSequence then
            ReplayOnline.Log("1.64.5 animation target has no sequences: id=" ..
                tostring(element.id) .. " name=" .. tostring(sequence))
            reportedOtherMissingSequence = true
        end
        return animateSequence(element, sequence, ...)
    end)
end

-- TU19 implements this NVIDIA Highlights prompt directly in its MP tabs;
-- 1.64.5 moved the same native calls into Lobby.DisplayNvidiaHighlightsButton.
-- Give the imported tabs that TU19 behavior only when its capability gate
-- succeeds. On systems without the feature the retail TU19 path does nothing.
if type(Lobby) == "table" and
    type(Lobby.DisplayNvidiaHighlightsButton) ~= "function" then
    rawset(Lobby, "DisplayNvidiaHighlightsButton", function(menu, group)
        if type(Engine.CGABICJHAI) ~= "function" or
            type(Engine.CFJEBEJCFC) ~= "function" or
            type(Dvar.IBEGCHEFE) ~= "function" or
            not Engine.CGABICJHAI() or
            not Dvar.IBEGCHEFE("LNQTQQPLRO") or
            not Engine.CFJEBEJCFC() then return end
        Engine.EGIICJGCF(group)
        Engine.EBEJAAJGDG(group)
        menu:registerEventHandler("nvidia_highlights_count", function(self, event)
            if (tonumber(event and event.group_count) or 0) <= 0 then return end
            self:AddButtonHelperText({
                side = "left", button_ref = "button_right_stick", priority = 6,
                helper_text = Engine.CBBHFCGDIC("LUA_MENU/NVIDIA_SUMMARY")
            })
            if self.bindButton then
                self.bindButton:registerEventHandler("button_right_stick", function()
                    Engine.BJFJDCFDID(group)
                end)
            end
        end)
    end)
end


-- The later MP menu asks Challenge.OpenDailyPopup during restore_focus.
-- TU19 exposes the same popup decision as TryOpenDailyPopup(controller).
-- Return a boolean for the later BR menu and mark this menu only if TU19
-- actually opened a popup, preserving its own challenge/checksum gates.
if type(Challenge) == "table" and
    type(Challenge.OpenDailyPopup) ~= "function" and
    type(Challenge.TryOpenDailyPopup) == "function" then
    rawset(Challenge, "OpenDailyPopup", function(menu, controller)
        local opened = Challenge.TryOpenDailyPopup(controller)
        if opened and menu then menu.openedDailyPopup = true end
        return not not opened
    end)
end

-- The 1.64.5 Weapons tab checks later vehicle horn/track item categories.
-- TU19's loot enum has no value for those categories; its native breadcrumb
-- function rejects nil. An absent category has no new items on this build.
if type(LOOT) == "table" and type(LOOT.IsCategoryNew) == "function" then
    local isCategoryNew = LOOT.IsCategoryNew
    local reportedMissingCategory = false
    rawset(LOOT, "IsCategoryNew", function(controller, itemType, ...)
        if itemType == nil then
            if not reportedMissingCategory then
                ReplayOnline.Log("1.64.5 loot category absent on TU19; skipping its new-item breadcrumb")
                reportedMissingCategory = true
            end
            return false
        end
        return isCategoryNew(controller, itemType, ...)
    end)
end

-- 1.64.5 always loads RestrictionsUtils before MPWeaponMenuButtons; TU19
-- omits that utility. Its two menu predicates depend only on these native
-- playlist dvars, so reproduce the original predicates without replacing
-- TU19's existing per-item MATCHRULES checks.
local menuRestrictions = Restrictions
if type(menuRestrictions) ~= "table" then
    menuRestrictions = {}
    rawset(_G, "Restrictions", menuRestrictions)
end
if type(menuRestrictions.AreSupersRestricted) ~= "function" then
    rawset(menuRestrictions, "AreSupersRestricted", function()
        return not Dvar.IBEGCHEFE("scr_game_allowsupers") or
            not Dvar.IBEGCHEFE("scr_war_allowsupers")
    end)
end
if type(menuRestrictions.AreKillStreaksRestricted) ~= "function" then
    rawset(menuRestrictions, "AreKillStreaksRestricted", function()
        return not Dvar.IBEGCHEFE("scr_game_allowkillstreaks") or
            not Dvar.IBEGCHEFE("scr_war_allowkillstreaks")
    end)
end

-- Test-only: read the original blade instance after MAIN_MENU data has been
-- applied. This distinguishes an absent titleLogo value from a missing
-- UpdateStaticState image assignment without replacing the original widget.
local originalBladeBuilder = MenuBuilder.m_types.mainmenublade
if type(originalBladeBuilder) == "function" then
    MenuBuilder.m_types.mainmenublade = function(...)
        local blade = originalBladeBuilder(...)
        local function hideEmptyShutterLogo()
            if blade and blade.TitleLogo and
                blade._bladeType == MAIN_MENU.BladeType.Shutter and
                blade._titleLogo == nil then
                -- The original source animates TitleLogo back to alpha 1 on
                -- ButtonOver even when this Shutter blade has no logo. Alter
                -- only that empty instance's stored Alpha tweens; otherwise
                -- selecting Multiplayer draws the default white 512x196 box.
                local sequences = blade.TitleLogo.sequences
                if type(sequences) == "table" then
                    for _, name in ipairs({"ButtonOver", "ButtonOverEnglish",
                        "ButtonUp", "ButtonUpEnglish"}) do
                        local frames = sequences[name]
                        if type(frames) == "table" then
                            for _, frame in ipairs(frames) do
                                if type(frame) == "table" and
                                    frame.property == TWEEN_PROPERTY.Alpha then
                                    frame.value = 0
                                end
                            end
                        end
                    end
                end
                blade.TitleLogo:SetAlpha(0, 0)
            end
        end
        hideEmptyShutterLogo()
        if blade and type(blade.Wait) == "function" then
            blade:Wait(250).onComplete = function()
                hideEmptyShutterLogo()
                ReplayOnline.Log("1.64.5 title blade state key=" ..
                    tostring(blade._bladeKey) .. " type=" ..
                    tostring(blade._bladeType) .. " logo=" ..
                    tostring(blade._titleLogo) .. " widget=" ..
                    tostring(blade.TitleLogo))
            end
        end
        return blade
    end
end
-- Replay has earlier stock images with these three names; native lookup picks
-- their flat placeholders even when the original 1.64.5 images are loaded.
-- The art FF therefore registers byte-identical source images under unique
-- names. Change only the original blade data's TitleLogo field before the
-- blade instances copy it; original menu bytecode and all other art stay intact.
local titleLogoAlias = {
    logo_cod_mw = "magma_ui1645_logo_cod_mw",
    logo_cod_store = "magma_ui1645_logo_cod_store",
    logo_cod_wz = "magma_ui1645_logo_cod_wz"
}
local mappedTitleLogos = 0
local visitedBladeData = {}
local function mapTitleLogo(node, depth)
    if type(node) ~= "table" or depth > 3 or visitedBladeData[node] then return end
    visitedBladeData[node] = true
    local originalName = rawget(node, "titleLogo")
    if type(originalName) == "string" and titleLogoAlias[originalName] then
        rawset(node, "titleLogo", titleLogoAlias[originalName])
        mappedTitleLogos = mappedTitleLogos + 1
    end
    for _, child in pairs(node) do
        if type(child) == "table" then mapTitleLogo(child, depth + 1) end
    end
end
for name, data in pairs(MAIN_MENU) do
    if type(name) == "string" and
        (string.match(name, "^BladeData") or string.match(name, "^OfflineBladeData")) then
        mapTitleLogo(data, 0)
    end
end
ReplayOnline.Log("1.64.5 original title logos mapped=" .. mappedTitleLogos)
-- Replay TU19 reports Core (mode 2) absent to the later title's install
-- service, although this exact isolated TU19 runtime has already entered and
-- played Multiplayer. Its native progress service also reports no install
-- manager. Treat the available target MP mode as installed for the imported
-- title's own blade actions; all other mode checks retain native behavior.
local mpMode = MAIN_MENU.GameModeEnumValues and MAIN_MENU.GameModeEnumValues.MP
if mpMode == CoD.PlayMode.Core and type(Engine.DBEGJIECGB) == "function" then
    local originalModeReady = Engine.DBEGJIECGB
    rawset(Engine, "DBEGJIECGB", function(mode, ...)
        if mode == mpMode then return true end
        return originalModeReady(mode, ...)
    end)
    ReplayOnline.Log("1.64.5 title Core install gate mapped to playable TU19 MP")
end

-- BEGIN native MP embedded-button install bridge.
-- The refreshed blade also gates its action on the later install-progress
-- service. TU19 PC has no install manager, but this runtime's native Core
-- session is available. Scope the translation to this one original action;
-- keep party checks, native menu entry, and all other install queries intact.
do
    local blade = MAIN_MENU.BladeDataEmbedButtons and
        MAIN_MENU.BladeDataEmbedButtons[MAIN_MENU.BladeKeys.MW]
    local button = blade and blade.buttonListData and blade.buttonListData[2]
    local action = button and button.buttonAction
    local _, mode
    if type(action) == "function" then _, mode = debug.getupvalue(action, 1) end
    if mode == CoD.PlayMode.Core and type(Engine.ECHHDAIBD) == "function" and
        ContentProgressState and ContentProgressState.INSTALLED ~= nil then
        button.buttonAction = function(...)
            local progress = Engine.ECHHDAIBD
            rawset(Engine, "ECHHDAIBD", function(requested, ...)
                if requested == mode then return ContentProgressState.INSTALLED end
                return progress(requested, ...)
            end)
            local result = {pcall(action, ...)}
            rawset(Engine, "ECHHDAIBD", progress)
            if not result[1] then error(result[2], 0) end
            return unpack(result, 2)
        end
        ReplayOnline.Log("1.64.5 embedded MP action bound to installed TU19 Core")
    end
end
-- END native MP embedded-button install bridge.

-- BEGIN native challenge-data bridge.
-- Keep TU19's real challenge parsing/progression and rewards. Its daily table
-- has no later Cold War category enum, so bind its original SetData closure
-- to the source widget's description field and omit the unsupported category.
do
    local nativeSetData = previous.challengessummaryitem
    for _, index in ipairs({1, 1}) do
        if type(nativeSetData) ~= "function" then break end
        local _, child = debug.getupvalue(nativeSetData, index)
        nativeSetData = child
    end
    assert(type(nativeSetData) == "function", "TU19 challenge SetData closure changed")
    local buildItem = MenuBuilder.m_types.wzchallengessummaryitem
    MenuBuilder.m_types.wzchallengessummaryitem = function(...)
        local widget = buildItem(...)
        widget.CategoryIcon:SetAlpha(0)
        widget.Title:setText("")
        widget.SetData = function(self, data, ...)
            local title = self.Title
            self.Title = self.Description
            local result = {pcall(nativeSetData, self, data, ...)}
            self.Title = title
            if not result[1] then error(result[2], 0) end
            return unpack(result, 2)
        end
        return widget
    end
    local buildSummary = MenuBuilder.m_types.challengessummary
    local _, nativePostLoad = debug.getupvalue(previous.challengessummary, 1)
    local _, sourcePostLoad = debug.getupvalue(buildSummary, 1)
    assert(type(nativePostLoad) == "function" and type(sourcePostLoad) == "function",
        "Challenge summary post-load closure changed")
    local _, nativeRefresh = debug.getupvalue(nativePostLoad, 3)
    local _, sourceRefresh = debug.getupvalue(sourcePostLoad, 3)
    assert(type(nativeRefresh) == "function" and type(sourceRefresh) == "function",
        "Challenge summary refresh closure changed")
    -- This original refresh reads GetCombinedDailyChallenges, applies the
    -- target's ordering and bounds the result to the constructed grid. The
    -- later GetDailyChallengeData service is absent from TU19.
    debug.setupvalue(sourcePostLoad, 3, nativeRefresh)
    MenuBuilder.m_types.challengessummary = function(...)
        if not CONDITIONS.IsWZWipFlowEnabled() then
            return previous.challengessummary(...)
        end
        -- The later daily tier-skip reward service has no TU19 counterpart.
        -- Keep the original challenge grid; only omit that optional panel.
        local frontend = CONDITIONS.IsWZWipFrontend
        if type(BR.GetNumTierSkips) ~= "function" then
            rawset(CONDITIONS, "IsWZWipFrontend", function() return false end)
        end
        local result = {pcall(buildSummary, ...)}
        rawset(CONDITIONS, "IsWZWipFrontend", frontend)
        if not result[1] then error(result[2], 0) end
        return unpack(result, 2)
    end
end
-- END native challenge-data bridge.
