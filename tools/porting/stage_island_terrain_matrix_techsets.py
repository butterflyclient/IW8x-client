"""Stage the Replay-compatible Island terrain VS and its complete techset pair.

The single admitted Source shader f28387a2... renders terrain main passes with a
two-row quaternion CB0 although Replay's prepass consumes four matrix rows.
Rewrite its exact DXBC program, preserve material/depth states and other assets,
rebuild native packed references and recompute only the affected PSO identities.

The map archive uses BOTH 28-byte and 20-byte state-metadata key contracts.
Detect and preserve each existing record's contract; never globally rekey it.
Outputs are isolated staging artifacts. No running game, installed file, DLL,
or user source tree is modified by this script.
"""
from __future__ import annotations

from collections import Counter
from datetime import datetime, timezone
import gc
import hashlib
import json
from pathlib import Path
import shutil
import struct

from build_island_vehicle_techsets import (
    PreloadArchive, ReplayArchive, complete, custom_container,
    decode, game, rebuild, require, sha, write_json, REPLAY_SHA256,
)
from island_terrain_instance_transforms import PROFILE, rewrite
from restore_island_vehicle_vertex_programs import (
    native_walk, pipeline_records, prove_rebuild, typed_edits, vertex_users,
)
from island_pipeline_keys import NAMESPACE, finalize

PROJECT = Path(__file__).resolve().parents[2]
STAGE = PROJECT / "build/flicker-investigation-20261022"
SOURCE = PROJECT / "build/vehicle-investigation-20261022/native-vertex-candidate-renderer-r2-20261022"
OUTPUT = STAGE / "terrain-matrix-replay-r1"
INPUT_SHADER = STAGE / "verified-terrain-color-vs.dxbc"
MAP_NAME = "techsets_mp_sm_island_1"
COMPANION_NAME = "techsets_magma_island_common_stream"
MAP_FF_SHA = "dceca0ba162c6e16fdada887f5ba63e8eb4b9e7fb9c7577e07e1fac2f86d9da1"
MAP_RAW_SHA = "5ff1deff1e1a99adff53962079964d6f10bf3b0b54cab242ca919682a38e990a"
COMPANION_FF_SHA = "cf438a309cf3b5f9ad1925f69d9a63eac4961d7181c3205fc36ea94a5a7e5a4d"
COMPANION_RAW_SHA = "7e734a532e13c63531ab13b682088ca1e639892ee2cc53e825e29ae7bb184f28"
OUTPUT_SHADER_SHA = "c45a8edea5da213e7a218019b2b80b4394128ff8c20956e99c10286e4de97664"
OUTPUT_SHADER_SIZE = 17900
MAP_ROOTS, COMPANION_ROOTS = 7314, 31
PIPELINE_RECORDS = 6530


def key_for(row: dict, payload: bytes, width: int) -> tuple[int, str]:
    require(width in (20, 28), "Unsupported Replay state metadata identity width")
    at = row["technique"]
    identity = dict(row["identity"])
    identity["state_metadata"] = payload[at + 156:at + 156 + width].hex()
    canonical = json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()
    digest = hashlib.sha256(NAMESPACE + canonical).digest()
    return int.from_bytes(digest[:8], "little"), digest.hex()


def original_widths(raw: bytes, registry: list[dict]) -> tuple[list[int], dict[str, int]]:
    widths = []
    for row in registry:
        old = int(row["old_key"], 16)
        matches = [width for width in (20, 28) if key_for(row, raw, width)[0] == old]
        require(len(matches) == 1, "Original PSO identity has no unique 20/28-byte state contract")
        widths.append(matches[0])
    counts = Counter(widths)
    require(len(widths) == PIPELINE_RECORDS and counts == Counter({20: 1452, 28: 5078}),
            "Mixed Replay PSO contract distribution changed")
    return widths, {str(k): v for k, v in sorted(counts.items())}


def rekey_owned_pipelines(original: bytes, relocated: bytes, old_rows: list[dict],
                          new_rows: list[dict], original_meta: list[int],
                          selected: dict[int, str]) -> tuple[bytes, list[dict]]:
    require(len(old_rows) == len(new_rows) == len(original_meta) == PIPELINE_RECORDS,
            "Native pipeline record order/count changed")
    raw = bytearray(relocated)
    changed, seen, identities, old_by_record = [], set(), {}, {}
    for old, current, width in zip(old_rows, new_rows, original_meta):
        require((old["root"], old["type"], old["techset"],
                 old["identity"]["layout_slot"], old["identity"]["format_row"]) ==
                (current["root"], current["type"], current["techset"],
                 current["identity"]["layout_slot"], current["identity"]["format_row"]),
                "PSO native asset/technique/variant ordering changed during relocation")
        require(old["old_key"] == current["old_key"],
                "Original PSO cache identity was lost during shader relocation")
        old_base, new_base = dict(old["identity"]), dict(current["identity"])
        old_shaders, new_shaders = old_base.pop("shaders"), new_base.pop("shaders")
        require(old_base == new_base, "Technique state, layout or material arguments changed")
        require(all(old_shaders[stage] == new_shaders[stage] for stage in ("HS", "DS", "PS")),
                "Unrelated hull/domain/pixel program changed")
        patched = current["technique"] in selected
        if patched:
            require(selected[current["technique"]] == PROFILE["name"] and
                    old_shaders["VS"]["sha256"] == PROFILE["input_sha256"] and
                    new_shaders["VS"]["sha256"] == OUTPUT_SHADER_SHA,
                    "Selected native VS shader identity differs")
        else:
            require(old_shaders["VS"] == new_shaders["VS"],
                    "Unselected native vertex program changed")
        next_key, digest = key_for(current, relocated, width)
        require(bool(next_key) and (next_key not in identities or identities[next_key] == digest),
                "A changed PSO identity collides with another renderer state")
        identities[next_key] = digest
        address = current["record"]
        require(address not in seen, "Duplicate PSO physical record")
        seen.add(address)
        was = int(current["old_key"], 16)
        require(struct.unpack_from("<Q", relocated, address)[0] == was,
                "Serialized PSO differs from the native baseline record")
        if patched:
            require(next_key != was, "Changed vertex shader retained a stale PSO key")
            struct.pack_into("<Q", raw, address, next_key)
            changed.append(dict(root=current["root"], techset=current["techset"],
                                type=current["type"], technique=current["technique"],
                                record=address, metadata_width=width, old=hex(was),
                                new=hex(next_key), identity_sha256=digest))
        else:
            require(next_key == was, "Unchanged pipeline identity differs from installed baseline")
    require(len(seen) == PIPELINE_RECORDS and changed,
            "Native PSO table incomplete or terrain shader has no consumers")
    result = bytes(raw)
    positions = {i for row in changed for i in range(row["record"], row["record"] + 8)}
    require(len(result) == len(relocated) and
            all(a == b or idx in positions for idx, (a, b) in enumerate(zip(relocated, result))),
            "PSO finalizer modified bytes outside selected keys")
    return result, changed


def validate_rekeyed(raw: bytes, archive: ReplayArchive, registry: list[dict],
                     widths: list[int], selected: dict[int, str]) -> tuple[dict, dict]:
    _, snapshot = finalize(raw, archive)
    rows = snapshot["rows"]
    require(len(rows) == len(registry) == len(widths) == PIPELINE_RECORDS,
            "Native PSO records differ after final replay load")
    counts = Counter()
    for old, new, width in zip(registry, rows, widths):
        require((old["root"], old["type"], old["techset"],
                 old["identity"]["layout_slot"], old["identity"]["format_row"]) ==
                (new["root"], new["type"], new["techset"],
                 new["identity"]["layout_slot"], new["identity"]["format_row"]),
                "Relocated pipeline owners differ after final native load")
        expected, _ = key_for(new, raw, width)
        require(expected == int(new["old_key"], 16),
                "Final PSO key fails its preserved old-version metadata contract")
        counts[width] += 1
    require(counts == Counter({20: 1452, 28: 5078}),
            "Final PSO mixed identity widths differ")
    return ({"records_checked": len(rows), "widths_checked": dict(counts),
             "selected_techniques": len(selected), "idempotent": True}, snapshot)


def main() -> None:
    require(not OUTPUT.exists(), "Output directory already exists; refuse overwrite")
    for file, expected in ((SOURCE / (MAP_NAME + ".ff"), MAP_FF_SHA),
                           (SOURCE / (MAP_NAME + ".raw"), MAP_RAW_SHA),
                           (SOURCE / (COMPANION_NAME + ".ff"), COMPANION_FF_SHA),
                           (SOURCE / (COMPANION_NAME + ".raw"), COMPANION_RAW_SHA)):
        require(file.is_file() and sha(file.read_bytes()) == expected,
                "Pinned renderer-r2 baseline differs: " + str(file))
    exe = game / "game_dx12_ship_replay.exe"
    with exe.open("rb") as stream:
        require(hashlib.file_digest(stream, "sha256").hexdigest() == REPLAY_SHA256,
                "Replay native executable identity differs")
    original = (SOURCE / (MAP_NAME + ".raw")).read_bytes()
    packed = (SOURCE / (MAP_NAME + ".ff")).read_bytes()
    require(decode(SOURCE / (MAP_NAME + ".ff")) == original,
            "Pinned original FF does not decode to the authenticated source")
    companion = (SOURCE / (COMPANION_NAME + ".raw")).read_bytes()
    require(decode(SOURCE / (COMPANION_NAME + ".ff")) == companion,
            "Pinned companion FF does not match its owned raw")
    shader = INPUT_SHADER.read_bytes()
    patched, shader_receipt = rewrite(shader, PROFILE["name"])
    require(sha(patched) == OUTPUT_SHADER_SHA and len(patched) == OUTPUT_SHADER_SIZE,
            "Pinned matrix projection output differs")

    OUTPUT.mkdir(parents=True, exist_ok=False)
    program_dir = OUTPUT / "vertex-programs"
    program_dir.mkdir()
    (program_dir / (PROFILE["name"] + ".dxbc")).write_bytes(patched)
    print("Matrix-compatible terrain shader:", len(patched), OUTPUT_SHADER_SHA, flush=True)

    map_dir = OUTPUT / "map_techsets"
    map_dir.mkdir()
    archive, before = native_walk(ReplayArchive, original, map_dir, "before", MAP_ROOTS)
    original_asset = [r for r in archive.assets if r["type"] == 14 and r["name"] == PROFILE["name"]]
    require(len(original_asset) == 1 and original_asset[0]["root"] == 184 and
            original_asset[0]["physical"] == 1790858,
            "Original Island terrain VS is not the registered root184/physical1790858")
    replacements = {PROFILE["name"]: {"before": shader, "after": patched}}
    original_users = vertex_users(archive, replacements)
    require(1 <= len(original_users) <= 64, "Terrain shader's original technique closure differs")
    records = pipeline_records(archive)
    for owners in records.values():
        require(not any(t in original_users for t in owners) or all(t in original_users for t in owners),
                "Pipeline records are shared between selected and unselected shaders")
    _, original_key_report = finalize(original, archive)
    require(original_key_report["pipeline_records"] == PIPELINE_RECORDS and
            not original_key_report["new_conflicting_keys"], "Native pipeline registry incomplete")
    widths, width_histogram = original_widths(original, original_key_report["rows"])
    edits, definitions = typed_edits(original, archive, replacements)
    require(len(definitions) == 1 and definitions[0]["header"] == 1790858 and
            definitions[0]["program"] == 1790931, "Typed shader-header or program owner differs")
    relocated, allocation_report = rebuild(original, before, edits)
    del archive
    gc.collect()
    print("Relocated one native program; native pointer/owner verification starting", flush=True)

    archive, after = native_walk(ReplayArchive, relocated, map_dir, "relocated", MAP_ROOTS)
    typed_changes = prove_rebuild(original, relocated, before, after, edits)
    updated_users = vertex_users(archive, replacements)
    require(Counter(updated_users.values()) == Counter(original_users.values()) and
            len(updated_users) == len(original_users),
            "Terrain technique owner closure changed during archive rebuilding")
    _, relocated_key_report = finalize(relocated, archive)
    final, changed_keys = rekey_owned_pipelines(original, relocated,
        original_key_report["rows"], relocated_key_report["rows"], widths, updated_users)
    require(len(changed_keys) >= len(updated_users), "Some native terrain pipelines were not rekeyed")
    del archive, before, after, original_key_report
    gc.collect()
    print("Rekeyed", len(changed_keys), "owned PSO variants;",
          "original state identity widths", width_histogram, flush=True)

    raw_file = OUTPUT / (MAP_NAME + ".raw")
    raw_file.write_bytes(final)
    archive, load = native_walk(ReplayArchive, final, map_dir, "load", MAP_ROOTS)
    final_users = vertex_users(archive, replacements)
    require(Counter(final_users.values()) == Counter(updated_users.values()),
            "Final native shader owners differ after rekeying")
    key_check, loaded_registry = validate_rekeyed(final, archive, relocated_key_report["rows"], widths, final_users)
    del relocated_key_report
    require(loaded_registry["pipeline_records"] == PIPELINE_RECORDS, "PSO registry changed after load")
    actual_key_by_record = {r["record"]: r for r in loaded_registry["rows"]}
    for change in changed_keys:
        present = actual_key_by_record.get(change["record"])
        require(present is not None and int(present["old_key"], 16) == int(change["new"], 16) and
                key_for(present, final, change["metadata_width"])[0] == int(change["new"], 16),
                "Changed PSO key not stable after native load")
    # Shared f283 terrain VS also serves legitimate GREATER_EQUAL color passes.
    # rekey_owned_pipelines already proves every technique's depth/other state
    # remains byte-identical to the baseline for ALL 6530 PSO variants.
    del archive
    gc.collect()
    archive, preload = native_walk(PreloadArchive, final, map_dir, "preload", MAP_ROOTS)
    del archive
    gc.collect()

    maximum = [max(a, b) for a, b in zip(load["maximum"], preload["maximum"])]
    result = bytearray(custom_container(packed[:136], final, maximum, preload["preload_walk_size"]))
    struct.pack_into("<I", result, 12, 4087)
    ff_file = OUTPUT / (MAP_NAME + ".ff")
    ff_file.write_bytes(result)
    require(decode(ff_file) == final, "Rebuilt native map FF failed decode round-trip")
    require(sha((SOURCE / (MAP_NAME + ".ff")).read_bytes()) == MAP_FF_SHA and
            sha((SOURCE / (MAP_NAME + ".raw")).read_bytes()) == MAP_RAW_SHA,
            "Renderer-r2 baseline changed during build")

    # The terrain shader is absent from the 31-root common-stream companion.
    # Ship both expected Island techset names in the package, with the companion
    # retained exactly to preserve its working vehicle shader programs.
    shutil.copyfile(SOURCE / (COMPANION_NAME + ".ff"), OUTPUT / (COMPANION_NAME + ".ff"))
    shutil.copyfile(SOURCE / (COMPANION_NAME + ".raw"), OUTPUT / (COMPANION_NAME + ".raw"))
    require(sha((OUTPUT / (COMPANION_NAME + ".ff")).read_bytes()) == COMPANION_FF_SHA and
            decode(OUTPUT / (COMPANION_NAME + ".ff")) == companion,
            "Unchanged companion FF differs")
    companion_dir = OUTPUT / "companion"
    companion_dir.mkdir()
    companion_archive, companion_load = native_walk(ReplayArchive, companion, companion_dir, "load", COMPANION_ROOTS)
    require(not any(a["type"] == 14 and a["name"] == PROFILE["name"] for a in companion_archive.assets),
            "Terrain shader unexpectedly imported by companion archive")
    del companion_archive
    gc.collect()
    companion_archive, companion_preload = native_walk(PreloadArchive, companion, companion_dir, "preload", COMPANION_ROOTS)
    del companion_archive
    gc.collect()

    receipt = {
        "status": "OFFLINE_BUILT_VALIDATED",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "renderer": "Replay 1.20.4",
        "baseline_game_executable_sha256": REPLAY_SHA256,
        "baseline_map_fastfile_sha256": MAP_FF_SHA,
        "baseline_map_raw_sha256": MAP_RAW_SHA,
        "shader": shader_receipt,
        "shader_program_definition_root": 184,
        "shader_original_header_physical": 1790858,
        "shader_original_program_physical": 1790931,
        "shader_definition_count": len(definitions),
        "selected_techniques": len(updated_users),
        "changed_pipeline_records": len(changed_keys),
        "previous_pipeline_identity_widths": width_histogram,
        "verified_pipeline_identities": key_check,
        "changed_pipeline_keys": changed_keys,
        "pipeline_records_verified": len(actual_key_by_record),
        "typed_allocations_preserved": True,
        "typed_shader_changes": typed_changes,
        "native_pointer_target_ownership_preserved": True,
        "native_load": {"complete": load["complete"], "roots": load["processed"], "bytes": load["physical_size"]},
        "native_preload": {"complete": preload["complete"], "roots": preload["processed"], "bytes": preload["physical_size"]},
        "map_fastfile": str(ff_file),
        "map_fastfile_bytes": len(result),
        "map_fastfile_sha256": sha(result),
        "map_raw_sha256": sha(final),
        "map_raw_bytes": len(final),
        "companion_fastfile": str(OUTPUT / (COMPANION_NAME + ".ff")),
        "companion_fastfile_sha256": COMPANION_FF_SHA,
        "companion_unchanged": True,
        "companion_native_load_roots": companion_load["processed"],
        "companion_native_preload_roots": companion_preload["processed"],
        "other_materials_pixel_shaders_depth_blend_and_arguments_preserved": True,
        "installed_game_files_modified": False,
        "runtime_processes_touched": False,
        "visual_result": "NOT_TESTED_GAME_CLOSED",
        "previous_depth_state_experiment_not_applied": True,
    }
    write_json(OUTPUT / "terrain-matrix-fastfile-receipt.json", receipt)
    print(json.dumps({k: receipt[k] for k in
          ("status", "selected_techniques", "changed_pipeline_records", "previous_pipeline_identity_widths",
           "map_fastfile", "map_fastfile_bytes", "map_fastfile_sha256",
           "companion_fastfile", "companion_fastfile_sha256", "visual_result")},
          indent=2), flush=True)


if __name__ == "__main__":
    main()
