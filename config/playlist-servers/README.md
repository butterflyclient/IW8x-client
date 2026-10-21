# Playlist server launchers

Start one server at a time. Close the previous dedicated server before choosing another .cmd.
Keep this folder in the game directory. The DLL supplies maps, mode settings, recipes and rules.
Wait for `Server ready`, then choose the matching playlist in Warzone or Multiplayer.
For a specific MP playlist, use the development Playlist Menu or check it in the Quick Play filter.

These launchers use a one-player lobby minimum for LAN testing. Round-based/team modes may need opponents.
Maps/modes rotate through installed playlist entries after matches. To pin entry N, replace `--playlist-rotate` with `--playlist-entry N`.
Entry indexes, native labels, modes and maps are listed in catalog.json. No campaign/co-op assets are available in this build.
The DLL scans mounted map fastfiles at startup and writes installed-maps.json with available entry indexes.
Missing map entries are skipped automatically; an empty playlist pool stops with a clear error and needs compatible map files.
An explicitly pinned unavailable entry is rejected. The scan checks presence, not every asset or streaming chunk.

| ID | Playlist | Max players | Entries | Launcher |
|---:|---|---:|---:|---|
| 1 | Team Deathmatch | 12 | 15 | [start-001-team-deathmatch.cmd](Multiplayer/start-001-team-deathmatch.cmd) |
| 2 | Free for All | 8 | 15 | [start-002-free-for-all.cmd](Multiplayer/start-002-free-for-all.cmd) |
| 3 | Domination | 12 | 15 | [start-003-domination.cmd](Multiplayer/start-003-domination.cmd) |
| 4 | Search and Destroy | 12 | 16 | [start-004-search-and-destroy.cmd](Multiplayer/start-004-search-and-destroy.cmd) |
| 5 | Headquarters | 12 | 18 | [start-005-headquarters.cmd](Multiplayer/start-005-headquarters.cmd) |
| 6 | Kill Confirmed | 12 | 16 | [start-006-kill-confirmed.cmd](Multiplayer/start-006-kill-confirmed.cmd) |
| 7 | Infected Shotguns | 18 | 14 | [start-007-infected-shotguns.cmd](Multiplayer/start-007-infected-shotguns.cmd) |
| 8 | Cyber Attack | 12 | 16 | [start-008-cyber-attack.cmd](Multiplayer/start-008-cyber-attack.cmd) |
| 9 | Hardpoint | 12 | 17 | [start-009-hardpoint.cmd](Multiplayer/start-009-hardpoint.cmd) |
| 10 | Realism Headquarters | 12 | 18 | [start-010-realism-headquarters.cmd](Multiplayer/start-010-realism-headquarters.cmd) |
| 11 | 10v10 Team Deathmatch | 20 | 10 | [start-011-10v10-team-deathmatch.cmd](Multiplayer/start-011-10v10-team-deathmatch.cmd) |
| 12 | 2v2 Arena | 16 | 1 | [start-012-2v2-arena.cmd](Multiplayer/start-012-2v2-arena.cmd) |
| 13 | 10v10 Headquarters | 20 | 5 | [start-013-10v10-headquarters.cmd](Multiplayer/start-013-10v10-headquarters.cmd) |
| 14 | 10v10 Kill Confirmed | 20 | 7 | [start-014-10v10-kill-confirmed.cmd](Multiplayer/start-014-10v10-kill-confirmed.cmd) |
| 15 | Gunfight Blueprints | 4 | 12 | [start-015-gunfight-blueprints.cmd](Multiplayer/start-015-gunfight-blueprints.cmd) |
| 16 | 3v3 Gunfight Knives | 6 | 13 | [start-016-3v3-gunfight-knives.cmd](Multiplayer/start-016-3v3-gunfight-knives.cmd) |
| 17 | Hardcore Team Deathmatch | 12 | 17 | [start-017-hardcore-team-deathmatch.cmd](Multiplayer/start-017-hardcore-team-deathmatch.cmd) |
| 18 | Hardcore Domination | 12 | 17 | [start-018-hardcore-domination.cmd](Multiplayer/start-018-hardcore-domination.cmd) |
| 19 | Hardcore Search and Destroy | 12 | 16 | [start-019-hardcore-search-and-destroy.cmd](Multiplayer/start-019-hardcore-search-and-destroy.cmd) |
| 20 | Hardcore Cyber Attack | 12 | 16 | [start-020-hardcore-cyber-attack.cmd](Multiplayer/start-020-hardcore-cyber-attack.cmd) |
| 21 | Hardcore Headquarters | 12 | 17 | [start-021-hardcore-headquarters.cmd](Multiplayer/start-021-hardcore-headquarters.cmd) |
| 22 | Hardcore Kill Confirmed | 12 | 15 | [start-022-hardcore-kill-confirmed.cmd](Multiplayer/start-022-hardcore-kill-confirmed.cmd) |
| 23 | Infected SMGs | 18 | 13 | [start-023-infected-smgs.cmd](Multiplayer/start-023-infected-smgs.cmd) |
| 24 | 10v10 Mosh Pit | 20 | 9 | [start-024-10v10-mosh-pit.cmd](Multiplayer/start-024-10v10-mosh-pit.cmd) |
| 25 | BR Trios | 150 | 1 | [start-025-br-trios.cmd](Warzone/start-025-br-trios.cmd) |
| 27 | Ground War Tavorsk District | 64 | 1 | [start-027-ground-war-tavorsk-district.cmd](Multiplayer/start-027-ground-war-tavorsk-district.cmd) |
| 28 | Ground War Karst River Quarry | 64 | 1 | [start-028-ground-war-karst-river-quarry.cmd](Multiplayer/start-028-ground-war-karst-river-quarry.cmd) |
| 29 | Ground War Krovnik Farmland | 64 | 1 | [start-029-ground-war-krovnik-farmland.cmd](Multiplayer/start-029-ground-war-krovnik-farmland.cmd) |
| 31 | Ground War Port of Verdansk | 64 | 1 | [start-031-ground-war-port-of-verdansk.cmd](Multiplayer/start-031-ground-war-port-of-verdansk.cmd) |
| 32 | Shipment Shoot House Rust Mosh Pit | 10 | 17 | [start-032-shipment-shoot-house-rust-mosh-pit.cmd](Multiplayer/start-032-shipment-shoot-house-rust-mosh-pit.cmd) |
| 33 | Realism Hardpoint | 12 | 17 | [start-033-realism-hardpoint.cmd](Multiplayer/start-033-realism-hardpoint.cmd) |
| 34 | Realism Domination | 12 | 19 | [start-034-realism-domination.cmd](Multiplayer/start-034-realism-domination.cmd) |
| 35 | Realism Kill Confirmed | 12 | 19 | [start-035-realism-kill-confirmed.cmd](Multiplayer/start-035-realism-kill-confirmed.cmd) |
| 36 | Hardcore 10v10 Mosh Pit | 20 | 11 | [start-036-hardcore-10v10-mosh-pit.cmd](Multiplayer/start-036-hardcore-10v10-mosh-pit.cmd) |
| 37 | Hardcore 10v10 Domination | 20 | 11 | [start-037-hardcore-10v10-domination.cmd](Multiplayer/start-037-hardcore-10v10-domination.cmd) |
| 39 | Ground War Zhokov Boneyard | 64 | 1 | [start-039-ground-war-zhokov-boneyard.cmd](Multiplayer/start-039-ground-war-zhokov-boneyard.cmd) |
| 40 | Giant Infected | 48 | 25 | [start-040-giant-infected.cmd](Multiplayer/start-040-giant-infected.cmd) |
| 41 | Gun Game Reloaded | 10 | 19 | [start-041-gun-game-reloaded.cmd](Multiplayer/start-041-gun-game-reloaded.cmd) |
| 42 | Capture the Flag | 12 | 13 | [start-042-capture-the-flag.cmd](Multiplayer/start-042-capture-the-flag.cmd) |
| 44 | Infected Assault Rifles | 18 | 14 | [start-044-infected-assault-rifles.cmd](Multiplayer/start-044-infected-assault-rifles.cmd) |
| 45 | CDL Hardpoint | 10 | 5 | [start-045-cdl-hardpoint.cmd](Multiplayer/start-045-cdl-hardpoint.cmd) |
| 46 | CDL Domination | 10 | 3 | [start-046-cdl-domination.cmd](Multiplayer/start-046-cdl-domination.cmd) |
| 47 | CDL Search and Destroy | 10 | 5 | [start-047-cdl-search-and-destroy.cmd](Multiplayer/start-047-cdl-search-and-destroy.cmd) |
| 48 | Hardcore Free for All | 8 | 15 | [start-048-hardcore-free-for-all.cmd](Multiplayer/start-048-hardcore-free-for-all.cmd) |
| 60 | Hardcore 10v10 Kill Confirmed | 20 | 10 | [start-060-hardcore-10v10-kill-confirmed.cmd](Multiplayer/start-060-hardcore-10v10-kill-confirmed.cmd) |
| 61 | Hardcore Shipment Shoot House Rust Cranked | 10 | 3 | [start-061-hardcore-shipment-shoot-house-rust-cranked.cmd](Multiplayer/start-061-hardcore-shipment-shoot-house-rust-cranked.cmd) |
| 62 | Hardcore Shipment Shoot House Rust Hardpoint | 10 | 3 | [start-062-hardcore-shipment-shoot-house-rust-hardpoint.cmd](Multiplayer/start-062-hardcore-shipment-shoot-house-rust-hardpoint.cmd) |
| 63 | Hardcore Shipment Shoot House Rust Kill Confirmed | 10 | 3 | [start-063-hardcore-shipment-shoot-house-rust-kill-confirmed.cmd](Multiplayer/start-063-hardcore-shipment-shoot-house-rust-kill-confirmed.cmd) |
| 64 | Hardcore Shipment Shoot House Rust Domination | 10 | 3 | [start-064-hardcore-shipment-shoot-house-rust-domination.cmd](Multiplayer/start-064-hardcore-shipment-shoot-house-rust-domination.cmd) |
| 67 | Demolition | 12 | 16 | [start-067-demolition.cmd](Multiplayer/start-067-demolition.cmd) |
| 71 | BR Buy Back Solos | 150 | 1 | [start-071-br-stimulus-solos.cmd](Warzone/start-071-br-stimulus-solos.cmd) |
| 89 | BR Quads | 152 | 1 | [start-089-br-quads.cmd](Warzone/start-089-br-quads.cmd) |
| 104 | Blood Money | 102 | 1 | [start-104-blood-money.cmd](Warzone/start-104-blood-money.cmd) |
