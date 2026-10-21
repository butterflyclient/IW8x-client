-- TU19 OnlineAAR table independently verified at registration RVA F277F0.
-- Keep native scoreboards, level progression fields and menu transitions.
if not ReplayOnline.Enabled() or type(ReplayOnline.AfterAction) ~= 'function' then return end
if rawget(_G, 'ReplayAfterAction') then return end
local state = {}; rawset(_G, 'ReplayAfterAction', state)
local bridge = ReplayOnline.AfterAction
local function pack(...) return {n=select('#',...), ...} end
local function frontend()
    return Engine.DDJFBBJAIG() and not ReplayOnline.IsSearching() and
        CONDITIONS.InFrontend() and not IsPrivateMatch() and not IsSystemLink()
end
if OnlineAAR and type(OnlineAAR.BIJEDCBCBH) == 'function' then
    local native = OnlineAAR.BIJEDCBCBH
    OnlineAAR.BIJEDCBCBH = function(controller, ...)
        local values = pack(native(controller, ...))
        local id = controller == 0 and bridge('state')
        if not id then return unpack(values, 1, values.n) end
        local info = {}; if type(values[1]) == 'table' then
            for key,value in pairs(values[1]) do info[key] = value end
        end
        -- LevelXP/Level belong to a separate native progression facility.
        -- Preserve them; do not guess their values from military rank.
        for _,field in ipairs({'StartXP','CurrentXP','StartSeasonXP','CurrentSeasonXP',
            'StartBattlepassXP','CurrentBattlepassXP','BattlepassTimeXP','BattlepassChallengeXP',
            'StartCPpoints','CurrentCPpoints','ChallengeXP','MissionXP','PlunderXP','MatchXP'}) do
            info[field] = tonumber((bridge('value',field))) or info[field] or 0
        end
        info.Unlocks, info.Challenges = {}, {}
        for index=1,4096 do
            local item=tonumber((bridge('unlock',tostring(index)))); if not item then break end
            info.Unlocks[#info.Unlocks+1]={ID=item}
        end
        local kinds={daily_mp='DAILY',daily_br='BR_DAILY',weekly_mp='WEEKLY',weekly_br='BR_WEEKLY',
            mission='MISSION',season='SEASON_REWARDS',gunsmith='GUNSMITH',reticle='RETICLE',
            operator='OPERATOR_UNLOCK',weapon='GUN_UNLOCK'}
        for index=1,4096 do
            local id,kind,count=bridge('challenge',tostring(index));if not id then break end
            local nativeKind=Challenge and Challenge.Type and Challenge.Type[kinds[kind] or '']
            if nativeKind then
                for tier=1,tonumber(count) or 0 do info.Challenges[#info.Challenges+1]={ID=tonumber(id),type=nativeKind} end
            end
        end
        values[1]=info;values.n=math.max(1,values.n);return unpack(values,1,values.n)
    end
end
if AAR and type(AAR.IsMatchClientDataAvailable)=='function' then
    local native=AAR.IsMatchClientDataAvailable
    AAR.IsMatchClientDataAvailable=function(...)
        local id,status=bridge('state')
        if id and status~='playing' and frontend() then return true end
        return native(...)
    end
end
if MenuBuilder and MenuBuilder.m_types then
    for _,name in ipairs({'mpmainmenu','brmainmenu','aarcontainer'}) do
        local build=MenuBuilder.m_types[name]
        if type(build)=='function' then
            local kind=name
            MenuBuilder.m_types[name]=function(parent,options)
                local results=pack(build(parent,options));local menu=results[1]
                local controller=(options and options.controllerIndex) or menu:getRootController() or 0
                if controller==0 then
                    if kind=='aarcontainer' then
                        local id,status=bridge('state')
                        if id and status~='playing' then bridge('ack',id);state.requested=id end
                    elseif not menu._useStandaloneStore then
                        bridge('refresh')
                        menu:addEventHandler('replay_aar_ready',function(self)
                            if not frontend() then return end
                            local visible=LUI.FlowManager.GetVisibleMenu()
                            if visible~=self then return end
                            local id,status,viewed=bridge('state')
                            if not id or status=='playing' or viewed=='1' or state.requested==id then return end
                            Lobby.OpenAfterActionReport(controller,false,true)
                            -- Do not ACK until the stock report is actually built.
                            if LUI.FlowManager.GetVisibleMenu()~=self then state.requested=id end
                        end)
                        menu:addElement(LUI.UITimer.new({interval=500,event='replay_aar_ready'}))
                    end
                end
                return unpack(results,1,results.n)
            end
        end
    end
end
