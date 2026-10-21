"""Package the verified TU19 progression tables for the backend worker (no game calls)."""
import csv,json
from pathlib import Path
root=Path(__file__).resolve().parents[2]
tables=root/'ida-reports/progression_native_tables'
def read(name):
    return list(csv.reader((tables/(name.replace('/','__')+'.csv')).open(encoding='utf-8')))
names=['mp/ranktable','mp/weaponranktable','mp/progression/progressionitems',
       'mp/progression/initialitems','mp/progression/elder3_progression','loot/weapon_ids',
       'loot/battlepass_season3','loot/battlepass_ids','loot/bundle_ids']
names += [p.stem.replace('__','/') for p in tables.glob('mp__gunsmith__*_progression.csv')]
data={'version':1,'season':3,'tables':{n+'.csv':read(n) for n in names}}
(root/'mw120rproxy/data/server_progression_catalog.json').write_text(json.dumps(data,separators=(',',':'))+'\n',encoding='utf-8')
print('Packaged',len(names),'server progression tables')
