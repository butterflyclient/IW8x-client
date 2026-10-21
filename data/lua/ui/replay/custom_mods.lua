-- The native Gunsmith/Armory widgets consume the server's committed builds.
-- Keep the retail blueprint service disabled; it has no working DW transport.
if not ReplayOnline.Enabled() or not Engine.DDJFBBJAIG() then return end
local getField = Gunsmith.CCJIIFCJHA
Gunsmith.CCJIIFCJHA = function(controller, path, ...)
    if controller == 0 and type(path) == "string" and path:sub(1, 11) == "blueprints." then
        local value = ReplayOnline.CustomModField(controller, path) or ""
        if path:match("%.variantID$") or path:match("%.lootItemID$") then return tonumber(value) or 0 end
        return value
    end
    return getField(controller, path, ...)
end
GUNSMITH.CanMakeModification = function(controller, weapon)
    local ready = ReplayOnline.CustomModsStatus()
    return controller == 0 and ready and GUNSMITH.AreAnyModSlotsUnlocked(controller, LOOT.GetItemID(LOOT.itemTypes.WEAPON, weapon))
end
local function quote(value)
    return '"' .. tostring(value):gsub('[%z\1-\31\\"]', function(c)
        return string.format('\\u%04x', string.byte(c))
    end) .. '"'
end
local function encode(value)
    if type(value) == "number" then return tostring(value) end
    if type(value) == "string" then return quote(value) end
    local result = {}
    if #value > 0 then
        for _, entry in ipairs(value) do result[#result + 1] = encode(entry) end
        return "[" .. table.concat(result, ",") .. "]"
    end
    for key, entry in pairs(value) do result[#result + 1] = quote(key) .. ":" .. encode(entry) end
    return "{" .. table.concat(result, ",") .. "}"
end
local function normalized(weapon)
    local result = {blueprintName = weapon.blueprintName, weapon = weapon.weapon,
        lootItemID = tonumber(weapon.lootID) or 0, variantID = tonumber(weapon.variantID) or 0,
        camo = weapon.camo or "none", cosmeticAttachment = weapon.cosmeticAttachment or "none",
        reticle = weapon.reticle or "none", attachmentSetup = {}, sticker = {}}
    for i = 1, 5 do
        local attachment = (weapon.attachments or {})[i] or {}
        result.attachmentSetup[i] = {attachment = attachment.ref or "none", variantID = tonumber(attachment.variantID) or 0}
    end
    for i = 1, 4 do result.sticker[i] = (weapon.stickers or {})[i] or "none" end
    return result
end
GUNSMITH.SaveAsCustomMod = function(controller, weapon, slot)
    return ReplayOnline.CustomModChange(controller, encode({action = "save", weapon = weapon.weapon, slot = slot, mod = normalized(weapon)}))
end
GUNSMITH.DeleteCustomMod = function(controller, weapon, slot)
    return ReplayOnline.CustomModChange(controller, encode({action = "delete", weapon = weapon, slot = slot}))
end

local messages = {pending = "Saving custom mod...", saved = "Custom mod saved.", deleted = "Custom mod deleted.",
    invalid_name = "Please enter a different custom mod name.",
    conflict = "Your mods changed on another client. Reopen this menu and try again.",
    invalid = "This custom mod could not be saved.", storage_error = "Unable to save the pending mod request. Check your players folder."}
local function toast(menu, controller, name, deleted)
    menu:dispatchEventToRoot({name = "send_toast_notification", immediate = true, options = {
        iconScale = -0.3, displayTime = 3000, icon = "icon_mod", iconColor = SWATCHES.genericMenu.notificationHighlight,
        header = Engine.CBBHFCGDIC(deleted and "CAS/MODIFICATION_DELETED" or "CAS/MODIFICATION_ADDED"),
        description = Engine.JCBDICCAH(GUNSMITH.GetCustomModName(name)),
        type = LUI.ToastManager.NotificationType.None, controllerIndex = controller}})
end
local build = MenuBuilder.BuildRegisteredType
MenuBuilder.BuildRegisteredType = function(name, options, ...)
    local kind = type(name) == "string" and string.lower(name) or ""
    if kind ~= "savecustommod" and kind ~= "deletecustommod" then return build(name, options, ...) end
    -- Capture only these menus' hold actions while the stock constructor runs.
    -- Success toasts, loadout labels and preview navigation happen after ACK.
    local hold, menu = LUI.AddUIHoldButtonLogic, nil
    LUI.AddUIHoldButtonLogic = function(button, controller, args)
        args.onFill = function() if menu and menu._replayModAction then menu._replayModAction(button.id, controller) end end
        return hold(button, controller, args)
    end
    local ok, result = pcall(build, name, options, ...)
    LUI.AddUIHoldButtonLogic = hold
    if not ok then error(result) end
    menu = result
    local opts = options or {}
    local scoped = LUI.FlowManager.GetScopedData(menu) or {}
    local function option(key) if opts[key] ~= nil then return opts[key] end; return scoped[key] end
    local label = LUI.UIText.new()
    label.id = "ReplayModSaveStatus"
    label:SetAnchorsAndPosition(0.5, 0.5, 0.5, 0.5, -700 * _1080p, 700 * _1080p, -260 * _1080p, -233 * _1080p)
    label:SetFont(FONTS.GetFont(FONTS.MainRegular.File))
    label:setText("")
    menu:addElement(label)
    local completion, pendingName
    if kind == "savecustommod" and menu.EnterName then
        menu.EnterName:registerEventHandler("button_action", function()
            if pendingName or completion then return end
            -- Keep the native keyboard; route its text to our authenticated host
            -- validator instead of the retail OSK's unavailable DW transport.
            OSK.CBJGJDGFC(option("controllerIndex") or 0, Engine.CBBHFCGDIC("CAS/ENTER_NAME"), menu._tempName or "", 17, false, false, true,
                function(_, value)
                    if value then
                        if ReplayOnline.ValidateModName(option("controllerIndex") or 0, value) then
                            pendingName = value
                            label:setText("Checking name...")
                        else label:setText("Name check unavailable. Please try again.") end
                    end
                end)
        end)
    end
    local function submitted(accepted, fn)
        if accepted then completion = fn; label:setText(messages.pending)
        else local _, _, _, state = ReplayOnline.CustomModsStatus(); label:setText(messages[state] or "Connecting to the server. Please try again.") end
    end
    menu._replayModAction = function(buttonID, controller)
        if pendingName then label:setText("Checking name..."); return end
        local ready, pending = ReplayOnline.CustomModsStatus()
        if completion or pending then label:setText(messages.pending); return end
        if not ready then label:setText("Connecting to the server. Please try again."); return end
        if kind == "savecustommod" then
            if not menu._tempName or #menu._tempName == 0 then
                LUI.FlowManager.RequestPopupMenu(nil, "NameWarningPopup", true, controller); return
            end
            local slot = option("modSlot")
            if buttonID == "CreateNewButton" then slot = GUNSMITH.GetFirstFreeCustomModSlot(controller, menu._weaponTable.weapon, LOOT.GetItemID(LOOT.itemTypes.WEAPON, menu._weaponTable.weapon)) end
            if type(slot) ~= "number" then
                LUI.FlowManager.RequestPopupMenu(nil, "CustomModSlotsFull", true, controller, false, {weaponRef = menu._weaponTable.weapon}); return
            end
            local weapon = LUI.DeepCopy(menu._weaponTable)
            weapon.blueprintName = GUNSMITH.GenerateModName(menu._tempName, slot)
            submitted(GUNSMITH.SaveAsCustomMod(controller, weapon, slot), function()
                local loadout = PlayerData.BFFBGAJGD(controller, LOADOUT.GetLoadoutStatsGroup()).squadMembers.loadouts[option("loadoutIndex")].weaponSetups[option("weaponIndex")]
                loadout.blueprintName:set(weapon.blueprintName)
                toast(menu, controller, weapon.blueprintName, false)
                local preview = LUI.DeepCopy(opts)
                preview.modSlot = slot; preview.weaponRef = weapon.weapon
                LUI.FlowManager.RequestAddMenu("ModificationPreview", true, controller, true, preview)
            end)
        else
            local function remove()
                local mod = menu._modification
                submitted(GUNSMITH.DeleteCustomMod(controller, mod.weapon, option("modSlot")), function()
                    -- Removing a saved build does not remove its equipped parts.
                    for _, loadout in ddlpairs(PlayerData.BFFBGAJGD(controller, LOADOUT.GetLoadoutStatsGroup()).squadMembers.loadouts) do
                        for _, weapon in ddlpairs(loadout.weaponSetups) do
                            if weapon.weapon:get() == mod.weapon and weapon.blueprintName:get() == mod.blueprintName then weapon.blueprintName:set("") end
                        end
                    end
                    toast(menu, controller, mod.blueprintName, true)
                    if option("closeLocker") then LUI.FlowManager.RequestLeaveMenuByName("WeaponLocker") end
                    LUI.FlowManager.RequestLeaveMenu(menu)
                end)
            end
            LUI.FlowManager.RequestPopupMenu(nil, "PopupYesNo", true, controller, false, {
                title = Engine.CBBHFCGDIC("MENU/CONFIRMATION_POPUP_TITLE"),
                message = Engine.CBBHFCGDIC("LUA_MENU/CUSTOM_MOD_DELETE"), yesAction = remove})
        end
    end
    menu:registerEventHandler("replay_custom_mod_result", function()
        if pendingName then
            local checking, complete, allowed, reason = ReplayOnline.ModNameStatus()
            if not checking and complete then
                local value = pendingName; pendingName = nil
                if allowed then
                    menu._tempName = value
                    menu.ModificationDisplay.ModName:setText(Engine.JCBDICCAH(ToUpperCase(value)))
                    ACTIONS.AnimateSequence(menu.ModificationDisplay, "ActiveName")
                    label:setText("")
                else
                    label:setText(reason == "unavailable" and "Name check unavailable. Please try again." or messages.invalid_name)
                end
            end
        end
        local ready, pending, _, state = ReplayOnline.CustomModsStatus()
        if not completion then return end
        label:setText(pending and messages.pending or messages[state] or "Waiting for the server...")
        if ready and not pending then
            local fn = completion; completion = nil
            if state == "saved" or state == "deleted" then fn() end
        end
    end)
    menu:addElement(LUI.UITimer.new({interval = 200, event = "replay_custom_mod_result"}))
    return menu
end
ReplayOnline.Log("server custom mods connected: save, update, delete and Armory")
