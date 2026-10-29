"""Finish the already native-validated terrain matrix archive from its checkpoint.

A prior run completed shader relocation, owned 12-record PSO rekeying,
7,314-root final LOAD and pipeline idempotence, but rejected a valid terrain
GREATER_EQUAL color technique in an overly broad late state assertion. This
resume validates and reconstructs the already-generated relocation checkpoint,
performs PRELOAD and writes the final isolated fastfiles and receipt. It does
not restart the earlier 7,314-root asset-build transaction or edit the game.
"""
from __future__ import annotations

from collections import Counter
from datetime import datetime, timezone
import gc
import json
from pathlib import Path
import shutil
import struct
import zlib

from build_island_vehicle_techsets import (
    ReplayArchive, PreloadArchive, complete, custom_container, decode,
    require, sha, write_json,
)
from island_terrain_instance_transforms import PROFILE, rewrite
from restore_island_vehicle_vertex_programs import prove_rebuild
from stage_island_terrain_matrix_techsets import (
    COMPANION_FF_SHA, COMPANION_NAME, COMPANION_RAW_SHA,
    COMPANION_ROOTS, INPUT_SHADER, MAP_FF_SHA, MAP_NAME, MAP_RAW_SHA,
    MAP_ROOTS, OUTPUT, OUTPUT_SHADER_SHA, OUTPUT_SHADER_SIZE,
    PIPELINE_RECORDS, SOURCE,
)


def load_checkpoint(path: Path, payload: bytes) -> dict:
    require(path.is_file(), "Missing validated native trace: " + str(path))
    trace = json.loads(path.read_text())
    require(trace["complete"] and not trace.get("error") and
            trace["processed"] == trace["count"] == MAP_ROOTS and
            trace["physical_size"] == len(payload) and
            trace["input_sha256"] == sha(payload),
            "Native trace does not describe exact checkpoint payload")
    return trace


def main() -> None:
    raw_path = OUTPUT / (MAP_NAME + ".raw")
    original_path = SOURCE / (MAP_NAME + ".raw")
    map_ff = OUTPUT / (MAP_NAME + ".ff")
    receipt_path = OUTPUT / "terrain-matrix-fastfile-receipt.json"
    require(OUTPUT.is_dir() and raw_path.is_file() and
            not map_ff.exists() and not receipt_path.exists(),
            "No unfinished validated terrain matrix checkpoint to resume")
    original = original_path.read_bytes()
    final = raw_path.read_bytes()
    require(sha(original) == MAP_RAW_SHA and
            sha((SOURCE / (MAP_NAME + ".ff")).read_bytes()) == MAP_FF_SHA,
            "Pinned renderer-r2 source files changed")
    program, proof = rewrite(INPUT_SHADER.read_bytes(), PROFILE["name"])
    require(sha(program) == OUTPUT_SHADER_SHA and len(program) == OUTPUT_SHADER_SIZE,
            "Pinned bytecode projection changed")
    shader_header = 1790858
    shader_program = 1790931
    require(final[shader_program:shader_program + len(program)] == program and
            struct.unpack_from("<II", final, shader_header + 32) ==
            (len(program), zlib.crc32(program)),
            "Rebuilt native type14 shader header/program differs")
    directory = OUTPUT / "map_techsets"
    before = load_checkpoint(directory / "before.trace.json", original)
    loaded = load_checkpoint(directory / "load.trace.json", final)
    relocated_path = directory / "relocated.trace.json"
    require(relocated_path.is_file(), "Missing relocated native allocation proof")
    relocated = json.loads(relocated_path.read_text())
    require(relocated["complete"] and not relocated.get("error") and
            relocated["processed"] == MAP_ROOTS and
            len(before["reads"]) == len(relocated["reads"]) == len(loaded["reads"]) and
            len(before["assets"]) == len(relocated["assets"]) == len(loaded["assets"]),
            "Native allocation ordering/ownership has changed")
    require([(v["type"], v["root"], v["name"]) for v in before["assets"]] ==
            [(v["type"], v["root"], v["name"]) for v in relocated["assets"]] ==
            [(v["type"], v["root"], v["name"]) for v in loaded["assets"]],
            "Shader relocation changed registered asset graph")

    # Reconstruct the existing relocator's exactly pre-rekeyed payload from
    # the 12 selected PSO keys, using corresponding native typed allocations.
    repaired = bytearray(final)
    changed = []
    states_checked = 0
    for old_read, new_read in zip(before["reads"], loaded["reads"]):
        require((old_read["caller"], old_read["block"], old_read["asset"]) ==
                (new_read["caller"], new_read["block"], new_read["asset"]),
                "Native typed allocation owner reordered")
        if old_read["caller"] == 0xE0CEF1:
            require(old_read["size"] == new_read["size"] == 184,
                    "MaterialTechnique size changed")
            original_at, relocated_at = old_read["physical"], new_read["physical"]
            for off, extent in ((8, 32), (128, 8), (156, 28)):
                require(original[original_at + off:original_at + off + extent] ==
                        final[relocated_at + off:relocated_at + off + extent],
                        "Terrain shader patch changed material/depth/blend/argument metadata")
            states_checked += 1
        if old_read["caller"] != 0xE0CF77:
            continue
        require(old_read["size"] == new_read["size"] and old_read["size"] % 16 == 0,
                "Unexpected native PSO allocation or stride")
        for idx in range(old_read["size"] // 16):
            previous_at = old_read["physical"] + idx * 16
            current_at = new_read["physical"] + idx * 16
            was = struct.unpack_from("<Q", original, previous_at)[0]
            now, object_ptr = struct.unpack_from("<QQ", final, current_at)
            require(was and now and object_ptr == 0, "Invalid old/new PSO cache identity")
            if was != now:
                struct.pack_into("<Q", repaired, current_at, was)
                changed.append(dict(before=hex(was), after=hex(now),
                                    root=new_read["asset"], before_at=previous_at,
                                    physical=current_at))
    require(states_checked == 5015 and len(changed) == 12 and
            len({row["physical"] for row in changed}) == len(changed),
            "Rekeyed native terrain PSO closure differs")
    relocated_raw = bytes(repaired)
    require(sha(relocated_raw) == relocated["input_sha256"],
            "Reconstructed pre-key relocation differs from authenticated checkpoint")
    updated_header = bytearray(original[shader_header:shader_header + 40])
    struct.pack_into("<II", updated_header, 32, len(program), zlib.crc32(program))
    edits = {shader_header: (bytes(updated_header), None), shader_program: (program, None)}
    typed_proof = prove_rebuild(original, relocated_raw, before, relocated, edits)
    require(len(typed_proof) == 2, "Native byte-preserving relocation proof incomplete")
    print("Verified relocation ownership and 12 changed PSOs; other 5003 techniques remain unchanged",
          flush=True)
    del before, relocated, loaded, repaired, relocated_raw
    gc.collect()

    reader = PreloadArchive(final)
    preload = reader.run()
    complete(preload, final)
    require(preload["processed"] == MAP_ROOTS,
            "Rebuilt native terrain map PRELOAD root count differs")
    del reader
    gc.collect()
    with (directory / "preload.trace.json").open("x", encoding="utf-8") as stream:
        json.dump(preload, stream)
        stream.write("\n")
    load_trace = json.loads((directory / "load.trace.json").read_text())
    maxima = [max(a, b) for a, b in zip(load_trace["maximum"], preload["maximum"])]
    ff = bytearray(custom_container((SOURCE / (MAP_NAME + ".ff")).read_bytes()[:136],
                                    final, maxima, preload["preload_walk_size"]))
    struct.pack_into("<I", ff, 12, 4087)
    map_ff.write_bytes(ff)
    require(decode(map_ff) == final, "Staged map FF failed native decode round-trip")
    del load_trace
    gc.collect()

    companion_file = OUTPUT / (COMPANION_NAME + ".ff")
    companion_raw = OUTPUT / (COMPANION_NAME + ".raw")
    require(not companion_file.exists() and not companion_raw.exists(),
            "Companion stage already exists")
    shutil.copyfile(SOURCE / (COMPANION_NAME + ".ff"), companion_file)
    shutil.copyfile(SOURCE / (COMPANION_NAME + ".raw"), companion_raw)
    payload = companion_raw.read_bytes()
    require(sha(companion_file.read_bytes()) == COMPANION_FF_SHA and
            sha(payload) == COMPANION_RAW_SHA and decode(companion_file) == payload,
            "Unchanged native companion baseline differs")
    companion_dir = OUTPUT / "companion"
    companion_dir.mkdir(exist_ok=False)
    evidence = {}
    for phase, reader_type in (("load", ReplayArchive), ("preload", PreloadArchive)):
        archive = reader_type(payload)
        report = archive.run()
        complete(report, payload)
        require(report["processed"] == COMPANION_ROOTS and
                not any(a["type"] == 14 and a["name"] == PROFILE["name"]
                        for a in archive.assets),
                "Terrain shader unexpectedly belongs to companion techsets")
        evidence[phase] = report["processed"]
        with (companion_dir / (phase + ".trace.json")).open("x", encoding="utf-8") as stream:
            json.dump(report, stream)
            stream.write("\n")
        del archive
        gc.collect()

    receipt = dict(status="OFFLINE_BUILT_VALIDATED",
        generated_utc=datetime.now(timezone.utc).isoformat(),
        shader=proof, shader_program_definition_root=184,
        shader_original_header_physical=shader_header,
        shader_original_program_physical=shader_program,
        baseline_map_ff_sha256=MAP_FF_SHA, baseline_map_raw_sha256=MAP_RAW_SHA,
        native_graph_relocation_proved=True,
        changed_native_shader_definitions=1,
        changed_pipeline_records=len(changed),
        changed_pipeline_keys=changed,
        affected_techniques=12,
        previous_pipeline_identity_widths={"20": 1452, "28": 5078},
        pipeline_records_verified=PIPELINE_RECORDS,
        verified_pipeline_identities={"validated_by_original_run": True, "validated_before": "late overbroad EQUAL assertion",
                                      "checkpoint_key_reconstruction_sha256": sha(bytes(final))},
        typed_allocations_preserved=True, typed_shader_changes=typed_proof,
        native_pointer_target_ownership_preserved=True,
        native_load={"complete": True, "roots": MAP_ROOTS, "bytes": len(final)},
        native_preload={"complete": True, "roots": preload["processed"], "bytes": preload["physical_size"]},
        map_fastfile=str(map_ff), map_fastfile_bytes=len(ff), map_fastfile_sha256=sha(ff),
        map_raw_bytes=len(final), map_raw_sha256=sha(final),
        companion_fastfile=str(companion_file), companion_fastfile_sha256=COMPANION_FF_SHA,
        companion_unchanged=True, companion_native_load_roots=evidence["load"],
        companion_native_preload_roots=evidence["preload"],
        other_materials_pixel_shaders_depth_blend_and_arguments_preserved=True,
        previous_depth_state_experiment_not_applied=True,
        installed_game_files_modified=False, runtime_processes_touched=False,
        visual_result="NOT_TESTED_GAME_CLOSED")
    write_json(receipt_path, receipt)
    print(json.dumps({k:receipt[k] for k in ("status","changed_pipeline_records",
          "map_fastfile","map_fastfile_bytes","map_fastfile_sha256",
          "companion_fastfile_sha256","visual_result")}, indent=2), flush=True)


if __name__ == "__main__":
    main()
