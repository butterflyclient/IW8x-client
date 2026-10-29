"""Prepare an installation-ready manifest/DLL companion for validated Island terrain FF.

The active Replay custom-fastfile loader authenticates name, size and SHA256.
Produce a new DLL by modifying only the single hash string and the matching
file_spec size fields. Preserve all DLL executable sections and all other file
specifications. Also stage the source include and evidence catalog as copies.
Never installs; use the existing reversible deploy_island_pipeline_keys.py
installer after inspecting the generated deployment-plan.json.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

PROJECT = Path(__file__).resolve().parents[2]
REPO = PROJECT.parents[1]
sys.path.insert(0, str(REPO / "tools/fastfiles"))
from deploy_island_pipeline_keys import FILES, NAME, closed, digest, pefile, save

OUTPUT = PROJECT / "build/flicker-investigation-20261022/terrain-matrix-replay-r1"
RECEIPT = OUTPUT / "terrain-matrix-fastfile-receipt.json"
RENDERER_DLL_SHA = "c37751e2e6f92d94217848142129f88978d9d4c4434202be462e6a9452f5ce9c"
EXISTING_MAP_SHA = "dceca0ba162c6e16fdada887f5ba63e8eb4b9e7fb9c7577e07e1fac2f86d9da1"
EXPECTED_MAP_SIZE = 27345580
EXPECTED_POINTERS = 3
EXPECTED_PROGRAM_SHA = "c45a8edea5da213e7a218019b2b80b4394128ff8c20956e99c10286e4de97664"


def require(condition: bool, text: str) -> None:
    if not condition:
        raise ValueError(text)


def main() -> None:
    closed()
    require(RECEIPT.is_file(), "Validated terrain matrix archive build receipt absent")
    info = json.loads(RECEIPT.read_text())
    map_ff = OUTPUT / NAME
    companion_ff = OUTPUT / "techsets_magma_island_common_stream.ff"
    require(info["status"] == "OFFLINE_BUILT_VALIDATED" and
            info["native_load"]["complete"] and info["native_preload"]["complete"] and
            info["native_load"]["roots"] == info["native_preload"]["roots"] == 7314 and
            info["shader"]["sha256"] == EXPECTED_PROGRAM_SHA and
            info["changed_pipeline_records"] == 12 and
            info["previous_pipeline_identity_widths"] == {"20": 1452, "28": 5078} and
            info["other_materials_pixel_shaders_depth_blend_and_arguments_preserved"],
            "Terrain shader/PSO native validation incomplete")
    require(map_ff.is_file() and digest(map_ff) == info["map_fastfile_sha256"] and
            map_ff.stat().st_size == info["map_fastfile_bytes"], "Candidate map FF identity mismatch")
    require(companion_ff.is_file() and digest(companion_ff) ==
            info["companion_fastfile_sha256"] and info["companion_unchanged"],
            "Companion techset differs from validated unchanged archive")
    require(FILES["fastfile"].stat().st_size == EXPECTED_MAP_SIZE and
            digest(FILES["fastfile"]) == EXISTING_MAP_SHA,
            "Installed game map FF changed from inspected source build")
    require(digest(FILES["dll"]) == RENDERER_DLL_SHA,
            "Installed loader DLL has changed: recompute exact PE manifest guards before staging")
    for filename in ("XInput9_1_0.dll", "island_probe_files.inc",
                     "staged-probe-catalog.json", "deployment-plan.json"):
        require(not (OUTPUT / filename).exists(), "Refusing existing staged artifact: " + filename)

    previous = FILES["dll"].read_bytes()
    old_hash = EXISTING_MAP_SHA.encode()
    new_hash = info["map_fastfile_sha256"].encode()
    require(len(old_hash) == len(new_hash) == 64 and previous.count(old_hash) == 1 and
            new_hash not in previous, "Compiled DLL manifest hash ownership differs")
    pe = pefile.PE(data=previous, fast_load=True)
    hash_at = previous.index(old_hash)
    hash_section = [s for s in pe.sections if s.PointerToRawData <= hash_at
                    and hash_at + 64 <= s.PointerToRawData + s.SizeOfRawData]
    require(len(hash_section) == 1 and not (hash_section[0].Characteristics & 0x20000000),
            "Compiled manifest SHA is outside non-executable PE data")
    hash_va = pe.OPTIONAL_HEADER.ImageBase + pe.get_rva_from_offset(hash_at)
    refs = [m.start() for m in re.finditer(re.escape(struct.pack("<Q", hash_va)), previous)]
    require(len(refs) == EXPECTED_POINTERS,
            "Unexpected number of shared SHA file_spec pointers in compiled DLL")
    candidate_size = map_ff.stat().st_size
    patched = bytearray(previous)
    patched[hash_at:hash_at + 64] = new_hash
    changes_allowed = set(range(hash_at, hash_at + 64))
    records = []
    for pointer_at in refs:
        require(pointer_at >= 16, "Invalid file_spec reference")
        filename_va, recorded_size = struct.unpack_from("<QQ", previous, pointer_at - 16)
        filename_rva = filename_va - pe.OPTIONAL_HEADER.ImageBase
        name_at = pe.get_offset_from_rva(filename_rva)
        actual_name = previous[name_at:previous.index(0, name_at)].decode("ascii")
        require(actual_name == NAME and recorded_size == EXPECTED_MAP_SIZE,
                "Compiled shared SHA used by a different file_spec or installed size")
        struct.pack_into("<Q", patched, pointer_at - 8, candidate_size)
        changes_allowed.update(range(pointer_at - 8, pointer_at))
        records.append(dict(pointer_field=pointer_at, size_field=pointer_at - 8,
                            name=actual_name, before_size=recorded_size, after_size=candidate_size))
    require(len(previous) == len(patched) and
            all(a == b or idx in changes_allowed
                for idx, (a, b) in enumerate(zip(previous, patched))),
            "Compiled DLL bytes outside the SHA/file size fields changed")
    for section in pe.sections:
        if section.Characteristics & 0x20000000:
            start, end = section.PointerToRawData, section.PointerToRawData + section.SizeOfRawData
            require(previous[start:end] == patched[start:end],
                    "Compiled executable DLL code was changed")
    PE_OFFSET = hash_at
    include = FILES["include"].read_bytes()
    pattern = rb'(\{ "techsets_mp_sm_island_1\.ff", )\d+(ull, ")[0-9a-f]{64}(" \},)'
    found = list(re.finditer(pattern, include))
    require(len(found) == 1, "Expected one Island map FF manifest include row")
    updated_include, count = re.subn(pattern, lambda m:
        m[1] + str(candidate_size).encode() + m[2] + new_hash + m[3], include)
    require(count == 1 and len(updated_include) > 100,
            "Source include update failed")
    catalog = json.loads(FILES["catalog"].read_text())
    rows = [r for r in catalog["files"] if r["name"] == NAME]
    require(len(rows) == 1, "Island catalog has no unique map FF entry")
    old_catalog = rows[0].copy()
    rows[0].update(size=candidate_size, sha256=new_hash.decode())

    (OUTPUT / "XInput9_1_0.dll").write_bytes(patched)
    (OUTPUT / "island_probe_files.inc").write_bytes(updated_include)
    save(OUTPUT / "staged-probe-catalog.json", catalog)
    plans = dict(
        status="STAGED", created_utc=datetime.now(timezone.utc).isoformat(),
        candidate_receipt=str(RECEIPT), candidate_receipt_sha256=digest(RECEIPT),
        originals={name: dict(path=str(path), sha256=digest(path)) for name, path in FILES.items()},
        targets={"fastfile": dict(path=str(map_ff), sha256=digest(map_ff)),
                 "dll": dict(path=str(OUTPUT / "XInput9_1_0.dll"), sha256=digest(OUTPUT / "XInput9_1_0.dll")),
                 "include": dict(path=str(OUTPUT / "island_probe_files.inc"),
                                 sha256=digest(OUTPUT / "island_probe_files.inc")),
                 "catalog": dict(path=str(OUTPUT / "staged-probe-catalog.json"),
                                 sha256=digest(OUTPUT / "staged-probe-catalog.json"))},
        manifest_offset=PE_OFFSET, manifest_size_offsets=[r["size_field"] for r in records],
        compiled_references=records, compiled_loader_dll_old_sha256=RENDERER_DLL_SHA,
        compiled_loader_dll_new_sha256=digest(OUTPUT / "XInput9_1_0.dll"),
        dll_executable_sections_unchanged=True,
        dll_changed_bytes=sum(a != b for a, b in zip(previous, patched)),
        previous_catalog_techset_row=old_catalog,
        previous_source_include_row=found[0].group().decode(),
        source_and_catalog_rows_were_stale_before_staging=True,
        no_runtime_renderer_code_changes=True, xpaks_changed=False,
        companion_techset_unchanged=True, game_closed_confirmed=True,
        install_command="py -3 -B ../../tools/fastfiles/deploy_island_pipeline_keys.py install " + str(OUTPUT),
        rollback_command="py -3 -B ../../tools/fastfiles/deploy_island_pipeline_keys.py rollback " + str(OUTPUT))
    save(OUTPUT / "deployment-plan.json", plans)
    print(json.dumps({key: plans[key] for key in
          ("status", "manifest_offset", "manifest_size_offsets", "compiled_loader_dll_new_sha256",
           "dll_executable_sections_unchanged", "dll_changed_bytes", "companion_techset_unchanged")},
          indent=2))


if __name__ == "__main__":
    main()
