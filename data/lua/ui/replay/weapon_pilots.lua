-- The original WeaponSelect widget enumerates the extended native statstable
-- and uses WEAPON.EquipBaseWeapon to save the real reference in native DDL.
-- No stock gun is substituted and no class is overwritten automatically.
if not ReplayOnline or not ReplayOnline.WeaponPilots or not ReplayOnline.WeaponPilots() then return end
local function ported(ref)
    return type(ref) == "string" and ReplayOnline.WeaponPorted(ref)
end
-- Missing per-weapon recipe fields are handled by the native restriction
-- bridge shared by UI and server scripts, preserving category restrictions.
local unlocked = PROGRESSION.IsUnlocked
PROGRESSION.IsUnlocked = function(controller, kind, ref, ...)
    if controller == 0 and kind == LOOT.itemTypes.WEAPON and ported(ref) then return true end
    return unlocked(controller, kind, ref, ...)
end
-- The converted base guns have no earned progression/attachment catalog
-- yet. Avoid calling TU19's native rank getter with an absent DDL weaponLevel
-- member. Stock progression continues through the existing backend adapter.
local weaponData = Gunsmith.BEDFIJHAB
Gunsmith.BEDFIJHAB = function(controller, ref, ...)
    if controller == 0 and ported(ref) then return {rank=0,xp=0} end
    return weaponData(controller, ref, ...)
end
local maximum = PROGRESSION.GetMaxWeaponLevel
PROGRESSION.GetMaxWeaponLevel = function(ref, ...)
    if ported(ref) then return 1 end
    return maximum(ref, ...)
end
local equip = WEAPON.EquipBaseWeapon
WEAPON.EquipBaseWeapon = function(setup, ref, ...)
    local result = equip(setup, ref, ...)
    if ported(ref) then
        local saved = setup.weapon:get()
        ReplayOnline.Log("ported loadout equip requested=" .. ref .. " saved=" .. tostring(saved))
        assert(saved == ref, "Ported weapon was not saved in the native loadout")
    end
    return result
end
ReplayOnline.Log("admitted ported weapons exposed in native weapon selection")
