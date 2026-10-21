-- Host selects the preview catalog; stock widgets own bundle presentation.
if not ReplayOnline.Enabled() or not Engine.DDJFBBJAIG() then return end
local categories, bundleIDs, available, titles = {}, {}, {}, {}
local ready, revision = false, 0
local function snapshot()
    local ok, version, count = ReplayOnline.StoreStatus()
    if not ok then return ready end -- keep the last validated catalog during a retry
    local rows, ids, allowed, names = {}, {}, {}, {}
    if ok then
        for index = 0, (tonumber(count) or 0) - 1 do
            local id, name, bundles = ReplayOnline.StoreCategory(index)
            if id then
                local key = "replay_store_host_" .. id
                local valid = {}
                for value in string.gmatch(bundles or "", "%d+") do
                    -- Check this installation has the referenced shipped metadata.
                    local csv = CSV.bundleIDs
                    local row = StringTable.BJJBBCJGEJ(csv.file, csv.cols.id, value)
                    if row and row >= 0 then
                        local data = CSV.ReadRow(csv, row)
                        local items = tonumber(data.numItems)
                        if tostring(data.id) == value and items and items > 0 and items <= 10
                            and data.image and data.image ~= "" and tonumber(data.currencyAmount)
                            and tonumber(data.currencyID) and (data.firstPartyProductID or "") == ""
                            and data.isBattlePassBundle ~= "1" and (data.purchaseEnd or "") == "" then
                            valid[#valid + 1] = value
                            allowed[tonumber(value)] = true
                        end
                    end
                end
                ids[key] = table.concat(valid, ",")
                local title = "REPLAY_STORE_TITLE_" .. id
                names[title] = name
                rows[#rows + 1] = {id = id, name = title, idPubVar = key,
                    numItemsShown = 8, items = {{layoutOnlyItem = true}}}
            end
        end
    end
    categories, bundleIDs, available, titles = rows, ids, allowed, names
    ready, revision = ok == true, tonumber(version) or 0
    return ready
end
snapshot()
local localize = Engine.CBBHFCGDIC
Engine.CBBHFCGDIC = function(key, ...)
    if titles[key] then return titles[key] end
    return localize(key, ...)
end
Engine.EHGIGBJJC = function() return {layoutType = StoreItemLayout.BILLBOARD_GRID} end
local getString = Dvar.DHEEJCCJBH
Dvar.DHEEJCCJBH = function(key, ...)
    if bundleIDs[key] ~= nil then return bundleIDs[key] end
    return getString(key, ...)
end
STORE.BundleAvailable = function(controller, id)
    return controller == 0 and ready and available[tonumber(id)] == true
end
local categoryTime = STORE.GetCategoryTimeLeft
STORE.GetCategoryTimeLeft = function(...)
    if type(Engine.DEIDGHDBHD()) ~= "number" then return 0, "", 0 end
    return categoryTime(...)
end
STORE.CheckForPersonalRecommendations = function() return nil end
STORE.IsItemOnSale = function() return false end

-- Refresh the provider snapshot at the data boundary. The original Store
-- constructors, preview actions, purchase popups and completion events remain.
Engine.DBFHBBIDHD = function() snapshot(); return LUI.DeepCopy(categories) end
ReplayOnline.Log("host catalog connected to native Store; native purchase providers active")
