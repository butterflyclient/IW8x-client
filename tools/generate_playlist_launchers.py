"""Generate one dedicated launcher per MP/Warzone playlist in the embedded TU19 catalog."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zlib

ROOT = Path(__file__).resolve().parents[1]
NAMES = {
    1: 'Team Deathmatch', 2: 'Free for All', 3: 'Domination', 4: 'Search and Destroy',
    5: 'Headquarters', 6: 'Kill Confirmed', 7: 'Infected Shotguns', 8: 'Cyber Attack',
    9: 'Hardpoint', 10: 'Realism Headquarters', 11: '10v10 Team Deathmatch',
    12: '2v2 Arena', 13: '10v10 Headquarters', 14: '10v10 Kill Confirmed',
    15: 'Gunfight Blueprints', 16: '3v3 Gunfight Knives', 17: 'Hardcore Team Deathmatch',
    18: 'Hardcore Domination', 19: 'Hardcore Search and Destroy', 20: 'Hardcore Cyber Attack',
    21: 'Hardcore Headquarters', 22: 'Hardcore Kill Confirmed', 23: 'Infected SMGs',
    24: '10v10 Mosh Pit', 25: 'BR Trios', 27: 'Ground War Tavorsk District',
    28: 'Ground War Karst River Quarry', 29: 'Ground War Krovnik Farmland',
    31: 'Ground War Port of Verdansk', 32: 'Shipment Shoot House Rust Mosh Pit',
    33: 'Realism Hardpoint', 34: 'Realism Domination', 35: 'Realism Kill Confirmed',
    36: 'Hardcore 10v10 Mosh Pit', 37: 'Hardcore 10v10 Domination',
    39: 'Ground War Zhokov Boneyard', 40: 'Giant Infected', 41: 'Gun Game Reloaded',
    42: 'Capture the Flag', 44: 'Infected Assault Rifles', 45: 'CDL Hardpoint',
    46: 'CDL Domination', 47: 'CDL Search and Destroy', 48: 'Hardcore Free for All',
    60: 'Hardcore 10v10 Kill Confirmed', 61: 'Hardcore Shipment Shoot House Rust Cranked',
    62: 'Hardcore Shipment Shoot House Rust Hardpoint',
    63: 'Hardcore Shipment Shoot House Rust Kill Confirmed',
    64: 'Hardcore Shipment Shoot House Rust Domination', 67: 'Demolition',
    71: 'BR Buy Back Solos', 89: 'BR Quads', 104: 'Blood Money',
}

def generate():
    compressed = (ROOT / 'data/playlists/unified_large_playlist_tu19.aggr').read_bytes()
    document = json.loads(zlib.decompress(compressed))
    playlists = [p for p in document['playlists'] if p['context'] in ('mp', 'gw', '2v2', 'big', 'bm')]
    assert {p['playlist'] for p in playlists} == NAMES.keys(), 'Update launcher labels for changed catalog'
    gametypes = {g['gametype']: g for g in document['gametypes']}
    outputs, rows = {}, []
    for p in sorted(playlists, key=lambda p: p['playlist']):
        id = p['playlist']
        group = 'Warzone' if p['context'] in ('big', 'bm') else 'Multiplayer'
        # Preserve the established launcher path for existing shortcuts.
        slug = 'br-stimulus-solos' if id == 71 else re.sub(r'[^a-z0-9]+', '-', NAMES[id].lower()).strip('-')
        path = f'{group}/start-{id:03d}-{slug}.cmd'
        outputs[path] = (f'''@echo off
setlocal
cd /d "%~dp0..\\.."
if not exist "game_dx12_ship_replay.exe" (
    echo Keep this script in playlist-servers\\{group} inside your Replay installation.
    pause
    exit /b 1
)
rem {NAMES[id]} - native playlist {id}. Map, script and rules come from the embedded .aggr.
rem Start one playlist server at a time on the configured matchmaking port.
rem --min-players 1 is for LAN testing; some modes require opponents to play a round.
start "IW8 Dedicated - {id} {NAMES[id]}" "%CD%\\game_dx12_ship_replay.exe" --dedi --playlist {id} --playlist-rotate --min-players 1 -bap +seta lui_skip_season_video 1 %*
''').replace('\n', '\r\n').encode('ascii')
        entries = []
        for index, entry in enumerate(p['maps']):
            g = gametypes[entry['gametype']]
            assert entry['map'].startswith('mp_') and not g['script'].startswith('cp_')
            entries.append(dict(index=index, map=entry['map'], gametype=entry['gametype'], script=g['script'],
                                weight=entry['weight'], recipe=bool(g.get('recipe')), hardcore='hardcore' in g['flags']))
        rows.append(dict(id=id, name=NAMES[id], native_name=p['name'], group=group, launcher=path,
                         max_players=p['dvars']['set party_maxplayers'], entries=entries))
    manifest = dict(source_sha256=hashlib.sha256(compressed).hexdigest(), playlists=rows)
    outputs['catalog.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    lines = ['# Playlist server launchers', '',
             'Start one server at a time. Close the previous dedicated server before choosing another .cmd.',
             'Keep this folder in the game directory. The DLL supplies maps, mode settings, recipes and rules.',
             'Wait for `Server ready`, then choose the matching playlist in Warzone or Multiplayer.',
             'For a specific MP playlist, use the development Playlist Menu or check it in the Quick Play filter.',
             '', 'These launchers use a one-player lobby minimum for LAN testing. Round-based/team modes may need opponents.',
             'Maps/modes rotate through installed playlist entries after matches. To pin entry N, replace `--playlist-rotate` with `--playlist-entry N`.',
             'Entry indexes, native labels, modes and maps are listed in catalog.json. No campaign/co-op assets are available in this build.',
             'The DLL scans mounted map fastfiles at startup and writes installed-maps.json with available entry indexes.',
             'Missing map entries are skipped automatically; an empty playlist pool stops with a clear error and needs compatible map files.',
             'An explicitly pinned unavailable entry is rejected. The scan checks presence, not every asset or streaming chunk.',
             '', '| ID | Playlist | Max players | Entries | Launcher |', '|---:|---|---:|---:|---|']
    lines += [f"| {r['id']} | {r['name']} | {r['max_players']} | {len(r['entries'])} | [{Path(r['launcher']).name}]({r['launcher']}) |" for r in rows]
    outputs['README.md'] = ('\n'.join(lines)+'\n').encode()
    return outputs

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    destination = ROOT / 'config/playlist-servers'
    outputs = generate()
    for name, content in outputs.items():
        path = destination / name
        if args.check:
            assert path.read_bytes() == content, f'Stale generated file: {path}'
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    actual = {p.relative_to(destination).as_posix() for p in destination.rglob('*.cmd')}
    assert actual == {p for p in outputs if p.endswith('.cmd')}, 'Unexpected/stale launcher'
    print(f'{"Checked" if args.check else "Generated"} 53 playlist launchers and catalog from embedded .aggr')
