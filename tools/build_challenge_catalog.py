"""Build reviewed offline challenge rules from this installation's TU19 assets.

Unknown predicates are reported, never approximated into easier requirements.
The output is embedded; CSV/localization extraction is a development step only.
"""
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TABLES = ROOT / 'ida-reports/progression_native_tables'
EXTRA = ROOT / 'ida-reports/progression_rule_tables'
LOC = json.loads((ROOT / 'ida-reports/progression_localization.json').read_text())
sources = {}


def rows(name, folder=TABLES):
    path = folder / (name.replace('/', '__') + '.csv')
    sources[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return list(csv.reader(path.open(encoding='utf-8')))


def ints(text):
    return [int(x) if x.isdigit() else 0 for x in text.split('|')]


def kill(**kw):
    return dict(event='kill', enemyAll=1, **kw)


def evt(event, **kw):
    return dict(event=event, **kw)


def param(index, values):
    return {str(index): [str(x) for x in values]}


weapons = {r[4]: r[1] for r in rows('mp/statstable', EXTRA) if len(r) > 4 and r[4] and r[1]}
attachments = rows('mp/attachmenttable', EXTRA)
# Native kill reports carry parent attachment references. Resolve those in the
# weapon's map (then its class fallback), since the same parent can name a
# different attachment on another gun: P90 gripvert is Granulated Grip Tape.
attachment_loc_path = ROOT/'ida-reports/progression-attachment-localization.json'
attachment_loc = json.loads(attachment_loc_path.read_text())
sources[str(attachment_loc_path.relative_to(ROOT))] = hashlib.sha256(attachment_loc_path.read_bytes()).hexdigest()
attachment_map_rows = rows('mp/attachmentmap')
attachment_map = {r[0]:r for r in attachment_map_rows[1:]}
attachment_assets = {r[4]:r[3] for r in attachments if len(r)>5}
named_attachments = {}
for weapon,cls in weapons.items():
    names = {}
    for index,parent in enumerate(attachment_map_rows[0][1:],1):
        def cell(key):
            row=attachment_map.get(key,[])
            return row[index] if index<len(row) else ''
        asset=cell(weapon) or cell(cls) or parent
        name=attachment_loc.get(attachment_assets.get(asset,''),'')
        if name in ['Granulated Grip Tape','Tactical Foregrip']:
            names.setdefault(name,[]).append(parent)
    if names:named_attachments[weapon]=names
attachment_names = {}
for r in attachments:
    if len(r) > 5:
        attachment_names.setdefault(r[3], set()).add(r[5])


def optic(prefix):
    return sorted({r[5] for r in attachments if len(r) > 5 and r[2] == 'optic' and r[3].startswith(prefix)})


conditions = {
    'GS_KILLS': kill(), 'CAMO_HEADSHOTS_DESC': kill(mask1=1048576),
    'CAMO_STANCECHANGE_DESC': kill(mask1=1024), 'CAMO_HIPFIRE_DESC': kill(mask1=16),
    'CAMO_DISTANCESHOT_DESC': kill(mask1=262144), 'CAMO_MOUNTED_DESC': kill(mask1=32),
    'CAMO_HOLDBREATH_DESC': kill(mask1=32), 'CAMO_FULLATTACHMENTS_DESC': kill(attachments=5),
    'CAMO_RELOADKILL_DESC': kill(mask1=65536), 'CAMO_NOATTACHMENTS_DESC': kill(attachments=0),
    'CAMO_STREAKS_DESC': kill(streak=3), 'CAMO_RIOT_STREAK_SMALL_DESC': kill(streak=2),
    'CAMO_INJURED_DESC': kill(mask1=4096), 'CAMO_BACKSTAB_DESC': kill(mask1=4),
    'CAMO_BUZZKILL_DESC': kill(mask1=67108864), 'CAMO_EXECUTION_DESC': kill(mask2=256),
    'CAMO_DOUBLEKILL_DESC': kill(rapid=2),
    'CAMO_POINTBLANK_DESC': kill(mask1=524288), 'CAMO_ONESHOT_DESC': kill(mask1=1),
    'CAMO_LAUNCHER_ATTACKER_DESC': kill(mask1=1073741824),
    'CAMO_LAUNCHER_DEFENDER_DESC': kill(mask1=4194304),
    'CAMO_LAUNCHER_AERIAL_DESC': evt('kill', enemyAll=6),
    'CAMO_LAUNCHER_GROUND_DESC': evt('kill', enemyAll=8, enemyAny=34),
    'CAMO_LAUNCHER_SPOTTER_DESC': evt('kill', enemyAny=98),
    'CAMO_LAUNCHER_SUPPORT_DESC': evt('kill', enemyAll=18),
    'CAMO_LAUNCHER_ALLKS_DESC': evt('kill', enemyAll=2),
    'CAMO_LAUNCHER_VEHICLE_DESC': evt('kill', enemyAll=32),
    'CAMO_LAUNCHER_WARHEAD_DESC': kill(perks=['specialty_warhead']),
    # Native item descriptions require Gold before Obsidian; dependency added below.
    'CAMO_OBSIDIAN_DESC': kill(perMatch=15, repeated=True),
    'CAMO_OBSIDIAN_RPG_DESC': kill(perMatch=5, repeated=True),
    'CAMO_OBSIDIAN_KNIFE_DESC': kill(perMatch=10, repeated=True),
    'CAMO_OBSIDIAN_LAUNCHER_DESC': evt('kill', enemyAny=34),
    'GUN_UNLOCK_CROSSBOW': kill(classes=['weapon_dmr'], attachmentGroups=[optic('ATTACHMENTS/REFLEX')], perMatch=5),
    'GUN_UNLOCK_HOLGER': kill(classes=['weapon_lmg'], mask1=262144, perMatch=2),
    'GUN_UNLOCK_TANGO': kill(classes=['weapon_assault'], mask1=1048576, perMatch=2),
    'GUN_UNLOCK_MIKE26': kill(classes=['weapon_shotgun'], attachments=5, perMatch=7),
    'GUN_UNLOCK_SMGOLF45': kill(classes=['weapon_smg'], mask1=256, perMatch=2),
    'GUN_UNLOCK_SIERRA552': kill(classes=['weapon_assault'], windowCount=5, windowMs=60000),
    # Near-enemy-smoke cannot be inferred from the native attacker smoke bit.
    'OPERATOR_UNLOCK_CHARLY': evt('end_match', params=param(1, [1, 2, 3])),
    'OPERATOR_UNLOCK_DOMINO': evt('end_match', params={'0': ['25'], '1': ['1']}),
    'OPERATOR_UNLOCK_GRINCH': kill(mask1=1048576),
    'OPERATOR_UNLOCK_KREUGER': kill(mask2=256),
    'OPERATOR_UNLOCK_MINOTAUR': kill(classes=['weapon_assault']),
    'OPERATOR_UNLOCK_RAINES': kill(classes=['weapon_lmg']),
}
for key, perk in [('CPAPA','gunperk_marksman'),('DECHO','fmj'),('GOLF21','fastreload'),
                  ('1911','gunperk_xp'),('PAPA320','ammomod_wound'),('MIKE9','gunperk_xp')]:
    conditions['AKIMBO_UNLOCK_'+key] = kill(attachmentGroups=[[perk]], perMatch=3)
conditions.update({
    'ROMEO870_ATTACHMENT_UNLOCK_1': kill(mask1=512|4194304),
    'ROMEO870_ATTACHMENT_UNLOCK_2': kill(mask1=8,attachmentGroups=[['stockno']],perMatch=5),
    'ROMEO870_ATTACHMENT_UNLOCK_3': kill(mask1=65536),
    'ROMEO870_ATTACHMENT_UNLOCK_4': kill(mask1=262144,attachmentGroups=[optic('ATTACHMENTS/FOURX')],perMatch=3),
    'BARREL_UNLOCK_UZULU': kill(mask1=262144,attachmentGroups=[['barlong','barcust2','barmid','barcust'],['stockh'],['calcust']],perMatch=2),
    'CALCUST_UNLOCK_UZULU': kill(mask1=16,attachmentGroups=[['xmagslrg']],perMatch=3),
})
simple_daily = {
    'KILLER': kill(), 'HEADHUNTER': kill(mask1=1048576), 'ASSISTER': evt('assist'),
    'CLOSE_AND_PERSONAL': kill(mask2=128), 'BLUNT_TRAUMA': kill(mask2=128),
    'ADS': kill(mask1=8), 'HIPSTER': kill(mask1=16), 'POINT_BLANK': kill(mask1=524288),
    'LONG_SHOT': kill(mask1=262144), 'ONE_SHOT': kill(mask1=1),
    'BACKUP_PLAN': kill(mask1=131072), 'HEADS_UP': kill(mask1=131072 | 1048576),
    'KARMA': kill(mask1=16384), 'REVENANT': kill(mask1=4096),
    'ALL_CLEAR': evt('kill', enemyAll=64), 'CAPTURE': evt('capture'),
    'PLAYA': evt('end_match', params=param(1, [1, 2, 3])),
    'WINNER': evt('end_match', params=param(1, [1])),
    'BLOODTHIRSTY_KILLER_I': kill(streak=5), 'THE_KING_IS_DEAD': kill(mask2=4),
    'DOUBLE_KILLER_I': kill(rapid=2),
    'HEARTBREAKER': kill(mask1=67108864), 'UNFORGIVING': kill(mask1=16777216),
    'STICKY_BOMBER': kill(mask2=8), 'BACKFIRE': kill(mask1=32768), 'EXECUTION': kill(mask2=256),
    'SCAVENGER': evt('end_match', valueParam=3), 'RESTOCK': evt('end_match', valueParam=4),
    'STIM': evt('stim', valueParam=0),
    'HS_MATCHES': kill(mask1=1048576, perMatch=1),
    'LAUNCHER_DESTROYS': evt('kill', enemyAll=32, classes=['weapon_projectile']),
    'SUPPORT_KILLSTREAK': evt('kill', enemyAll=18, classes=['weapon_projectile']),
    'AERIAL_KILLSTREAK': evt('kill', enemyAll=6),
    'GROUND_KILLSTREAK': evt('kill', enemyAll=8, enemyAny=34),
}
for name, ref in [('DEAD_SILENCE','super_deadsilence'),('AMMO_DROP','super_ammo_drop'),
                  ('TROPHY_SYSTEM','super_trophy'),('RECON_DRONE','super_recon_drone'),
                  ('SUPPORT_BOX','super_support_box'),('TACTICAL_INSERTION','super_tac_insert'),
                  ('EMP_DRONE','super_emp_drone'),('WEAPON_DROP','super_weapon_drop')]:
    simple_daily[name]=evt('field_end',params=param(0,[ref]),valueParam=1)
simple_daily['TUNE_UP']=evt('field_end')
simple_daily['INBOUND']=evt('killstreak_end')
for name, ref in [('PERSONAL_UAV','radar_drone_overwatch'),('UAV','uav'),('COUNTER_UAV','scrambler_drone_guard'),('CARE_PACKAGE','airdrop')]:
    simple_daily[name]=evt('killstreak_end',params=param(0,[ref]))
for name, ref in [('HIGH_ALERT','specialty_surveillance'),('BATTLE_HARDENED','specialty_tac_resist')]:
    simple_daily[name]=kill(perks=[ref])
for word, count in [('ZERO_TOLERANCE', 0), ('ONE_TRICK_PONY', 1), ('TWO_TO_TANGO', 2),
                    ('THREE_CHEERS', 3), ('FOUR_LEAF_CLOVER', 4), ('HIGH_FIVE', 5)]:
    simple_daily[word] = kill(attachments=count)
for name, cls, short in [('ASSAULT_RIFLE','assault','AR'),('SMG','smg','SM'),('LMG','lmg','LM'),
                        ('SHOTGUN','shotgun','SH'),('SNIPER','sniper','SN'),('PISTOL','pistol','PI'),('LAUNCHER','projectile','LA')]:
    simple_daily[name + '_KILLS'] = kill(classes=['weapon_' + cls])
    simple_daily[name + '_HEADSHOTS'] = kill(classes=['weapon_' + cls], mask1=1048576)
    simple_daily[name + '_KILLS_HEADSHOTS'] = kill(classes=['weapon_' + cls], mask1=1048576)
    simple_daily[short + '_KILLS_MATCHES'] = kill(classes=['weapon_' + cls] + (['weapon_dmr'] if short == 'SN' else []), perMatch=3)
for name, mode in [('TEAM_DEATHMATCH',6),('SEARCH_DESTROY',7),('DOMINATION',8),('CYBER_ATTACK',26),('HEADQUARTERS',35),('GUNFIGHT',25)]:
    simple_daily[name+'_WINS'] = evt('end_match', params={'0':[str(mode)],'1':['1']})
for name, ref in [('AR_GUN_1','ar_scharlie'),('AR_GUN_2','ar_mcharlie'),('AR_GUN_3','ar_akilo47'),
                  ('SM_GUN_1','sm_papa90'),('SM_GUN_2','sm_mpapa7'),('SM_GUN_3','sm_uzulu'),
                  ('SH_GUN_1','sh_dpapa12'),('SH_GUN_2','sh_romeo870'),('SN_GUN_1','sn_sbeta'),('LM_GUN_1','lm_pkilo')]:
    if 'iw8_'+ref in weapons: simple_daily[name] = kill(weapon='iw8_'+ref)
for key, rule in simple_daily.items(): conditions['DAILY_'+key+'_DESC'] = rule
br = {
    'EASY_MONEY': evt('pickup', params=param(1,[1])),
    'COLLECTOR': evt('pickup', params=param(0,[1]), perMatch=1),
    'DISCRIMINATING_TASTES': evt('loadout'),
    'TREASURE_HUNTER': evt('pickup', params=param(2,[1])),
    'TEST_DRIVE': evt('driving', valueParam=0, unit=60000),
    'HAZARDOUS_WORK_ENVIRONMENT': evt('alive_in_gas', valueParam=0, unit=1000),
    'SURVIVOR': evt('alive_not_downed', valueParam=0, unit=60000),
    'WELL_PROTECTED': evt('use_item', params=param(0,['armor_plate'])),
    # onlaststandrevive(reviver, recipient): self is (1,1), teammate is (1,0).
    'CAREGIVER': evt('revive', params={'0':['1'],'1':['0']}),
    'BOUNCEBACKALICIOUS': evt('revive', params={'0':['1'],'1':['1']}),
    'UNKILLABLE': evt('revive', params={'0':['0'],'1':['1']}),
    'IN_CHARGE_OF_THE_YARD': evt('gulag_end_match', params=param(0,[1])),
    'EXECUTIONER': kill(mask2=2048),
    'BEST_OF_THE_BEST': evt('match_placement', ranges={'1':[1,15]}),
}
for key, rule in br.items(): conditions['BR_DAILY_'+key+'_DESC'] = rule
# Event parameters below are emitted by scripts/cp_mp/challenges.gsc. The
# equipment reference is the first element of kill parameter 1 after weaponfixup.
lethals=[r[1] for r in rows('loot/equipment_ids') if len(r)>6 and r[6]=='1']
equipment={
    'FRAG':'equip_frag','SEMTEX':'equip_semtex','MOLOTOV_COCKTAIL':'equip_molotov',
    'CLAYMORE':'equip_claymore','THROWING_KNIFE':'equip_throwing_knife','C4':'equip_c4',
    'THERMITE':'equip_thermite','PROXIMITY_MINE':'equip_at_mine'}
for key,ref in equipment.items():conditions['DAILY_'+key+'_DESC']=kill(weaponRefs=[ref])
conditions.update({
    'DAILY_WELL_EQUIPPED_DESC':kill(weaponRefs=lethals),
    'DAILY_SHRAPNEL_DESC':kill(weaponRefs=lethals,perks=['specialty_extra_shrapnel']),
    'DAILY_FLASH_GRENADE_DESC':evt('stun',params=param(0,['equip_flash'])),
    'DAILY_STUN_GRENADE_DESC':evt('stun',params=param(0,['equip_concussion'])),
    'DAILY_EOD_DESC':evt('hack'),
    'DAILY_COLD_BLOODED_DESC':evt('kill',enemyAll=2,perks=['specialty_covert_ops']),
    'DAILY_SPOTTER_DESC':evt('kill',enemyAny=98,perks=['specialty_tactical_recon']),
    'DAILY_QUICKSCOPE_DESC':kill(mask1=2048),
    'DAILY_GHOST_DESC':kill(mask2=1,perks=['specialty_guerrilla']),
    'BR_DAILY_TAKING_INITIATIVE_DESC':kill(ranges={'13':[0,0]}),
    'BR_DAILY_MISSION_FOCUSED_DESC':evt('contract_end',params={'1':['1'],'3':['1']}),
})
# Verified in recordkillstreakendstats (2246.gsc): parameter 3 is kills;
# parameters 1/2 are duration/end time and must never award kills.
conditions['DAILY_STREAKER_DESC']=evt('killstreak_end',valueParam=3)
for key,ref in [('SHIELD_TURRET','manual_turret'),('CLUSTER_STRIKE','toma_strike'),
                ('CRUISE_MISSILE','cruise_predator'),('PRECISION_AIRSTRIKE','precision_airstrike'),
                ('ASSAULT_TANK','bradley'),('SENTRY_GUN','sentry_gun'),('WHEELSON','pac_sentry')]:
    conditions['DAILY_'+key+'_DESC']=evt('killstreak_end',params=param(0,[ref]),valueParam=3)
# BR challenge events carry authoritative loot rarity and kiosk item categories.
for key,rarity in [('CRUSHING_WITH_COMMONS',0),('RAMPAGE_WITH_RARES',2),
                   ('EXTERMINATE_WITH_EPICS',3),('LETHAL_WITH_LEGENDARIES',4)]:
    conditions['BR_DAILY_'+key+'_DESC']=kill(params=param(12,[rarity]))
conditions.update({
    'BR_DAILY_THE_BIGGEST_BOOMS_DESC':evt('buy_item',params=param(0,['killstreak'])),
    'BR_DAILY_ARMS_DEALER_DESC':evt('buy_item',params={'0':['special'],'1':['supply_drop']}),
    'BR_DAILY_AMMO_DEALER_DESC':evt('buy_item',params={'0':['fieldUpgrade'],'1':['super_ammo_drop']}),
    'BR_DAILY_PHILANTHROPIST_DESC':evt('buy_item',params=param(0,['teamrevive'])),
    'BR_DAILY_TURNING_THE_TABLES_DESC':evt('contract_end',params={'0':['assassination'],'1':['1'],'3':['2']}),
})
for key,category in [('HEADHUNTER','assassination'),('CONQUERER','domination'),
                     ('EXPLORER','scavenger'),('GUARDIAN','vip')]:
    conditions['BR_DAILY_'+key+'_DESC']=evt('contract_end',params={'0':[category],'1':['1'],'3':['1']})
# setupchallengelocales assigns these IDs. Contract parameter 1 is the tablet's
# locale list, which can contain multiple overlapping volumes.
for key,locale in [('AIRFIELD_ASCENDENCY',1),('BONEYARD_BADASS',2),('DAM_DANGERZONE',4),
                   ('DOWNTOWN_DOMINATION',5),('FARM_FATALITY',6),('GULAG_GOVERNOR',7),
                   ('HOSPITAL_HOPLITE',8),('LAYOVER_LEGEND',10),('LUMBERYARD_LUCHADOR',11),
                   ('PORT_PATROL',15),('QUARRY_QUEST',16),('STADIUM_SUPERSTAR',20),
                   ('STORAGE_SOVEREIGN',21),('SUPERCENTER_SLAYER',31),('TRAINYARD_TYRANT',32),
                   ('TELEVISION_TRIUMPH',33)]:
    conditions['BR_DAILY_'+key+'_DESC']=evt('contract_start',paramContains=param(1,[locale]))
# Broad optic groups use the same native attachment families as reticles.
for key,prefix in [('REFLEX','REFLEX'),('ACOG','FOURX'),('HOLO','HOLO')]:
    conditions['DAILY_ATTACHMENT_'+key+'_DESC']=kill(attachmentGroups=[optic('ATTACHMENTS/'+prefix)])
laser_refs=sorted({r[5] for r in attachments if len(r)>5 and r[3].startswith('ATTACHMENTS/LASER')})
for key,refs in [('DRUMMAG',['drums','calsmgdrums']),('LASER',laser_refs),('NOSTOCK',['stockno']),
                 ('MUZZLE',['brake']),('FMJ',['fmj']),('SOH',['fastreload'])]:
    conditions['DAILY_ATTACHMENT_'+key+'_DESC']=kill(attachmentGroups=[refs])
conditions['DAILY_ATTACHMENT_HYBRID_DESC']=kill(attachmentGroups=[sorted({r[5] for r in attachments if len(r)>5 and
    r[3].startswith('ATTACHMENTS/HYBRID') and not r[3].startswith('ATTACHMENTS/HYBRID_THERMAL')})])
for key,left,right in [('ACOG_FMJ','ACOG','FMJ'),('NOSTOCK_DRUM','NOSTOCK','DRUMMAG'),
                       ('NOSTOCK_LASER','NOSTOCK','LASER'),('FMJ_LASER','FMJ','LASER')]:
    conditions['DAILY_ATTACHMENT_'+key+'_DESC']=kill(attachmentGroups=
        conditions['DAILY_ATTACHMENT_'+left+'_DESC']['attachmentGroups']+
        conditions['DAILY_ATTACHMENT_'+right+'_DESC']['attachmentGroups'])
# Reuse only IDENTICAL shipped descriptions for weekly/mission definitions.
def normalize(text):
    text=re.sub(r'\^[0-9]','',text).replace('&&1','N').lower().replace('(s)','s').replace('(es)','es')
    return re.sub(r'\s+',' ',text).rstrip('. ')
by_text = {normalize(LOC['CHALLENGE/'+key]): rule for key, rule in conditions.items() if 'CHALLENGE/'+key in LOC}


def simple_text(text):
    s=normalize(text)
    if s in by_text: return by_text[s]
    # Whole-string templates only: never ignore a remaining qualifier.
    n=r'(?:n|\d+)'
    for pattern,rule in [
        (rf'(?:get |kill ){n} (?:public match )?(?:kills|enemies)',kill()),
        (rf'(?:get |win ){n} public (?:wins|match|matches)',evt('end_match',params=param(1,[1]))),
        (rf'(?:complete|infil and complete|play) {n} public (?:match|matches)',evt('end_match',params=param(1,[1,2,3]))),
        (r'call in a killstreak',evt('killstreak_end')),
        (rf'execute {n} finishing moves',kill(mask2=256)),
    ]:
        if re.fullmatch(pattern,s):return rule
    for name,cls in [('assault rifle','assault'),('smg','smg'),('submachine gun','smg'),('shotgun','shotgun'),
                     ('lmg','lmg'),('light machine gun','lmg'),('sniper rifle','sniper'),('pistol','pistol'),('marksman rifle','dmr')]:
        if re.fullmatch(rf'get {n} kills (?:with|using) (?:an? |any )?{name}s?',s):return kill(classes=['weapon_'+cls])
        if re.fullmatch(rf'get {n} headshot(?: kills)?s? (?:with|using) (?:an? |any )?{name}s?',s):return kill(classes=['weapon_'+cls],mask1=1048576)
    for pattern,rule in [
        (rf'get {n} melee kills',kill(params=param(11,['MOD_MELEE']))),
        (rf'get {n} kills (?:with|using) (?:your )?secondary weapon',kill(mask1=131072)),
        (rf'get {n} kills while aiming down sights',kill(mask1=8)),
        (rf'get {n} long ?shot kills',kill(mask1=262144)),
        (rf'(?:get|kill) {n} (?:kills|enemies) (?:with|using) killstreaks',evt('killstreak_end',valueParam=3)),
        (rf'get {n} kills (?:with|using) (?:a weapon with |weapons with )?([0-5]) attachments?(?: (?:equipped|on a weapon))?',None),
        (rf'(?:kill|destroy) {n} (?:enemy )?killstreaks',evt('kill',enemyAll=2)),
        (rf'destroy {n} vehicles',evt('kill',enemyAll=32)),
        (rf'capture an objective in objective games {n} times',evt('capture')),
        (rf'get {n} triple kills \(3 rapid kills\)',kill(rapid=3)),
        (rf'get {n} fury kills \(4 rapid kills\)',kill(rapid=4)),
        (rf'get {n} kills (?:with|using) lethal equipment',kill(weaponRefs=lethals)),
        (rf'get {n} kills with ghost perk enabled',kill(perks=['specialty_guerrilla'])),
        (rf'using ghost, get {n} kills',kill(perks=['specialty_guerrilla'])),
        (rf'using shrapnel, get {n} kills using lethal equipment',kill(weaponRefs=lethals,perks=['specialty_extra_shrapnel'])),
        (rf'using overkill, get {n} kills with your secondary',kill(mask1=131072,perks=['specialty_munitions_2'])),
        (rf'(?:using spotter, hack|hack) {n} pieces of equipment(?: with the spotter perk)?',evt('hack',paramContains=param(1,['specialty_tactical_recon']))),
    ]:
        match=re.fullmatch(pattern,s)
        if match:return rule if rule is not None else kill(attachments=int(match[1]))
    for name,ref in [('shield turrets?','manual_turret'),('cluster strikes?','toma_strike'),
                     ('precision airstrikes?','precision_airstrike'),('wheelsons?','pac_sentry'),
                     ('chopper gunners?','chopper_gunner')]:
        if re.fullmatch(rf'get {n} kills with (?:a )?{name}',s):return evt('killstreak_end',params=param(0,[ref]),valueParam=3)
    for name,mode in [('team deathmatch',6),('search and destroy',7),('domination',8),('cyber attack',26),('headquarters',35),('gunfight',25)]:
        if re.fullmatch(rf'(?:get {n} wins? in|win {n} matches of) {name}',s):return evt('end_match',params={'0':[str(mode)],'1':['1']})
        if re.fullmatch(rf'(?:complete|play) {n} {name} matches',s):return evt('end_match',params={'0':[str(mode)],'1':['1','2','3']})
        if re.fullmatch(rf'get {n} kills in {name}',s):return kill(params=param(5,[mode]))
    match=re.fullmatch(rf'get (\d+) kills without dying(?: {n} times)?',s)
    if match:return kill(streak=int(match[1]))
    return None

bindings = {}
mastery_bindings = {}
for path in sorted(TABLES.glob('mp__gunsmith__*_progression.csv')):
    weapon = 'iw8_' + path.stem.removeprefix('mp__gunsmith__').removesuffix('_progression')
    if weapon not in weapons: continue
    for row in rows(path.stem):
        if not row or not row[0].lstrip('-').isdigit(): continue
        for c in range(1,len(row)-3,4):
            if row[c+1].isdigit():
                bindings[int(row[c+1])] = {'weapon':weapon,'level':max(0,int(row[0])), 'loot':ints(row[c])}
                if row[c+2].endswith(('_gold_camo','_diamond_camo','_black_camo')):
                    mastery_bindings[(weapon,row[c+2].rsplit('_',2)[-2])] = (int(row[c+1]),ints(row[c])[0])

from reviewed_challenge_rules import build as reviewed_rules
reviewed = reviewed_rules(kill, evt, param, optic, conditions)
for name in ['mw120rproxy/tools/reviewed_challenge_rules.py',
             'ida-reports/native_bot_decompiled/1843.gsc',
             'ida-reports/native_bot_decompiled/2244.gsc',
             'ida-reports/native_bot_decompiled/2420.gsc',
             'ida-reports/native_bot_decompiled/scripts__cp_mp__challenges.gsc.gsc']:
    sources[name] = hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
mission_previous={r[7]:(int(r[0]),max(ints(r[6]))) for r in rows('quest_challenges') if r[7]}
rules, unsupported, excluded = [], [], []
elder_levels={int(r[8]):960000+max(1,int(r[1])) for r in rows('mp/progression/elder3_progression') if len(r)>8 and r[8].isdigit()}
types = [('gs_challenges','gunsmith',4,6,5),('gun_unlock_challenges','weapon',4,5,6),
         ('op_unlock_challenges','operator',4,None,5),('reticle_challenges','reticle',4,6,5),
         ('daily_challenges','daily_mp',4,5,6),('br_daily_challenges','daily_br',4,5,6),
         ('weekly_challenges','weekly_mp',4,5,6),('br_weekly_challenges','weekly_br',4,5,6),
         ('quest_challenges','mission',6,8,9),('elder_challenges','elder',4,6,5)]
for file,kind,amount,xp,loot in types:
    for r in rows(file):
        if not r or not r[0].isdigit() or not r[amount] or max(ints(r[amount])) <= 0: continue
        id = int(r[0]); key = r[3]; description = LOC.get(key.upper(),'')
        if kind=='elder' and id not in elder_levels:continue
        if kind == 'mission' and (r[5] not in ['ALL','BR'] or r[16] not in ['0','1']):
            excluded.append({'id':id,'kind':kind,'family':r[5],'category':r[16],'season':r[17],
                             'description':description,'reason':'Mission family/entitlement adapter not implemented'})
            continue
        if kind=='gunsmith' and id not in bindings:continue
        condition = reviewed.get(id) or conditions.get(key.removeprefix('CHALLENGE/')) or simple_text(description)
        if kind=='reticle':
            scope=r[7]
            options=sorted(attachment_names.get(scope,[]))
            generic={'ATTACHMENTS/REFLEX':'ATTACHMENTS/REFLEX','ATTACHMENTS/HOLO_GENERIC':'ATTACHMENTS/HOLO',
                     'ATTACHMENTS/ACOG_GENERIC':'ATTACHMENTS/FOURX','ATTACHMENTS/HYBRID_GENERIC':'ATTACHMENTS/HYBRID',
                     'ATTACHMENTS/THERMAL_GENERIC':'ATTACHMENTS/THERMAL'}
            if scope in generic:options=optic(generic[scope])
            if scope=='ATTACHMENTS/HYBRID_GENERIC':
                options=sorted({r[5] for r in attachments if len(r)>5 and r[3].startswith('ATTACHMENTS/HYBRID') and not r[3].startswith('ATTACHMENTS/HYBRID_THERMAL')})
            if scope=='ATTACHMENTS/SNPRSCOPE':options=sorted(set(options+['default_sniper_scope']))
            text=normalize(description)
            if options:
                condition=kill(attachmentGroups=[options])
                if 'headshots' in text:condition['mask1']=1048576
                if 'longshot' in text:condition['mask1']=262144
                if 'mounted' in text:condition['mask1']=32
                if 'without dying' in text or 'single life' in text:condition['streak']=3
                if 'double kills' in text:condition['rapid']=2
        if not condition:
            unsupported.append({'id':id,'kind':kind,'description':description,'key':key})
            continue
        rule = dict(condition)
        rule.update(id=id, kind=kind, ref=r[1], desc=key, tiers=ints(r[amount]),
                    xp=ints(r[xp]) if xp is not None else [0], loot=ints(r[loot]))
        rule['scope'] = 'br' if kind.endswith('_br') or (kind=='mission' and r[5]=='BR') else 'mp' if kind.endswith('_mp') or kind=='operator' else 'any'
        if kind == 'gunsmith':
            if id not in bindings: continue # retired placeholder rows are not unlocks
            rule.update(bindings[id])
            if 'OBSIDIAN' in key:
                gold=mastery_bindings.get((rule['weapon'],'gold'))
                if not gold:
                    unsupported.append({'id':id,'kind':kind,'description':description,'key':key,'reason':'Missing native Gold definition'})
                    continue
                rule['requiresLoot']=[gold[1]]
        if kind == 'mission':
            rule['nextRef']=r[7]; rule['category']=int(r[16]); rule['season']=int(r[17] or '0')
            # Unsupported predecessors still gate the next stage. The previous
            # implementation indexed only supported rows and could skip a stage.
            if r[1] in mission_previous:
                previous,target=mission_previous[r[1]]
                rule['requiresProgress']={str(previous):target}
        if kind.startswith('weekly'): rule['season']=int(r[9] or '0')
        if kind=='elder':rule['minimumXP']=elder_levels[id]
        rules.append(rule)

# The native reticle list is a sequence within each optic family. A kill that
# finishes one stage is not also credited to its newly available successor.
reticle_groups = {}
for r in rows('reticle_challenges'):
    group,step=r[1].rsplit('_',1)
    reticle_groups.setdefault(group,[]).append((int(step),int(r[0]),int(r[4])))
for group in reticle_groups.values():
    group.sort()
    for index,(_,id,_) in enumerate(group):
        if index:
            _,previous,target=group[index-1]
            for rule in rules:
                if rule['id']==id:rule['requiresProgress']={str(previous):target}

mastery=[]
gs={int(r[0]):r for r in rows('gs_challenges') if r and r[0].isdigit()}
# Require every shipped standard camo challenge, including any predicate still
# awaiting implementation. Missing rule coverage must never award early Gold.
for (weapon,kind),(id,loot) in mastery_bindings.items():
    if kind!='gold':continue
    required={str(cid):max(ints(gs[cid][4])) for cid,b in bindings.items()
        if b['weapon']==weapon and cid in gs and gs[cid][4] and
        'CAMO_OBSIDIAN' not in gs[cid][3] and '_camo_' in gs[cid][1]}
    if required:mastery.append(dict(id=id,loot=loot,progress=required,items=[]))
base_gold={w:v[1] for (w,k),v in mastery_bindings.items() if k=='gold' and v[0] in gs}
def mastery_class(w):
    c=weapons.get(w)
    return 'weapon_sniper_dmr' if c in ('weapon_sniper','weapon_dmr') else c
for (weapon,kind),(id,loot) in mastery_bindings.items():
    if kind=='gold':continue
    needed=sorted(v for w,v in base_gold.items() if kind=='black' or mastery_class(w)==mastery_class(weapon))
    own=mastery_bindings.get((weapon,'gold'))
    if own:needed=sorted(set(needed+[own[1]]))
    if needed:mastery.append(dict(id=id,loot=loot,progress={},items=needed))
output = {'version':1,'weapons':weapons,'namedAttachments':named_attachments,'rules':rules,'mastery':mastery,'sources':sources}
dest = ROOT/'mw120rproxy/data/challenge_catalog.json'
dest.write_text(json.dumps(output,indent=2)+'\n',encoding='utf-8')
report={'implemented':len(rules),'unresolved':len(unsupported),'rulesByKind':{},'unresolvedRules':unsupported,
        'excludedMissionRules':excluded,'sources':sources}
for rule in rules: report['rulesByKind'][rule['kind']]=report['rulesByKind'].get(rule['kind'],0)+1
(ROOT/'ida-reports/progression_challenge_coverage.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
remaining=[
    'IW8 SERVER PROGRESSION - REMAINING OBJECTIVE COVERAGE',
    'Generated by mw120rproxy/tools/build_challenge_catalog.py from shipped TU19 tables.',
    '',
    f"Selected families: {len(rules)} supported rules; {len(unsupported)} unresolved rules.",
    'These counts include archived daily/weekly definitions; only Season 3 weeklies are active.',
    'They do not claim completion of every challenge family in the game.',
    '',
    'UNRESOLVED IN THE SELECTED FAMILIES',
    'Needs additional verified dedicated facts or predicate semantics. Progress is not fabricated.',
    'Examples: enemy near smoke, weapon-specific Dead Silence, recon/EMP targets,',
    'vehicle occupants/assists, pickup origin, same-explosion kills and BR ping types.',
]
for r in unsupported:
    remaining.append(f"{r['id']} [{r['kind']}] {r['description'] or r['key']}")
remaining+=['','OTHER MISSION FAMILIES NOT IMPLEMENTED',
            'These require their mode/entitlement adapters in addition to event matching.',
            'Operator rows span multiple seasons; 60 belong to Season 3.',
            'CP/Intel and retired platform-exclusive rows are not ordinary MP objectives.']
for family in sorted({r['family'] for r in excluded}):
    group=[r for r in excluded if r['family']==family]
    remaining.append(f"{family}: {len(group)} rows; IDs " + ', '.join(str(r['id']) for r in group))
remaining+=['','OTHER PROGRESSION LIMITS',
            '- Dedicated DDL projection and abrupt-server-exit durability of queued awards remain separate work.',
            '- Remote Radmin PC validation of the new objective/token build remains pending.',
            '- XP tokens are implemented; see XP_TOKENS_BACKEND.md for migration and timer behavior.','']
(ROOT/'PROGRESSION_REMAINING.txt').write_text('\n'.join(remaining),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ['unresolvedRules','excludedMissionRules','sources']},indent=2))
