"""Append verified vehicle image/curve roots to Island's existing dependency.

The existing script strings, root markers and pointer ownership are retained.
The native LOAD trace supplies the three packed-reference relocations when
the root table grows. New roots must be self-contained. Both final native
loading paths must reach EOF before the new fastfile is packaged.
"""
from __future__ import annotations

import argparse
import gc
from pathlib import Path
import struct

from restore_island_vehicle_images import (REPLAY_SHA256, file_sha,
                                           read_json, require, sha, write_json)
from replay_walk import ReplayArchive, decode, game
from preload120.walk_preload import PreloadArchive
from port_archive import custom_container
from record_writer import rebuild


COMMON_RAW = Path(r"D:\Games\iw8x-port-stage\island-120-20261013\analysis\common164"
                  r"\magma_island_common_stream.geometry.raw")
COMMON_TRACE = COMMON_RAW.with_suffix(".load.json")
COMMON_RAW_SHA256 = "51b8b9461941c68f7a79377241fad683397c91037537acf0557f2d306091e81c"
COMMON_TRACE_SHA256 = "111b59082ecf79ec62a74ad034545406f8b74db2930ecb027c244f23228fae16"
COMMON_FF_SHA256 = "06d2dacaa5a0492e28dabdd26d6721563551d6846515b2bb9365b2bb77219765"
RELOCATED_PREFIX_SHA256 = "92373a9a0f0f0438ca72d1434b5754499a01143732d409e368d9c8e38cd7860e"
IMAGE_RAW_SHA256 = "e1f7c79844fdc64e3cc5f55ba153c01846930866ed2d78c77cae53050e451eba"
INLINE = 0xfffffffffffffffe
ROOT_TABLE_AT = 1494333
ROOT_TABLE_BYTES = 28 * 16
CURVE_NAMES = ("ncs_xac_common_br_mp", "iw8_mp_veh_decho_door_back_left",
               "iw8_mp_veh_decho_door_front_left", "iw8_mp_veh_decho_door_front_right")


def root_body(raw, types):
    require(raw[:16] == bytes(16) and struct.unpack_from("<QQ", raw, 16) == (len(types), INLINE),
            "Appended root archive must have no external script-string table")
    table = b"".join(struct.pack("<IIQ", kind, 0, INLINE) for kind in types)
    require(raw[32:32 + len(table)] == table, "Appended root table differs")
    return raw[32 + len(table):]


def complete(result, size, count):
    require(result["complete"] and not result.get("error") and
            result["count"] == result["processed"] == count and result["physical_size"] == size,
            "Native archive traversal is incomplete or did not reach exact EOF")


def walk(reader, raw, output, phase, old_names, added_names):
    archive = reader(raw)
    try:
        report = archive.run()
    except Exception as error:
        archive.error = str(error)
        report = archive.report()
    report["payload_sha256"] = sha(raw)
    if phase == "load":
        report["assets"] = archive.assets
    write_json(output / (phase + ".trace.json"), report)
    complete(report, len(raw), 46)
    if phase == "load":
        retained = [(a["type"], a.get("name"), a["root"]) for a in archive.assets if a["root"] < 28]
        added = [(a["type"], a.get("name"), a["root"]) for a in archive.assets if a["root"] >= 28]
        require(retained == old_names and added == added_names, "Native registered owner identities changed")
    summary = {key: report[key] for key in ("complete", "count", "processed", "error", "physical_size",
               "maximum", "preload_walk_size") if key in report}
    del archive, report
    gc.collect()
    print("COMBINED_NATIVE", phase, summary, flush=True)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images-dir", type=Path, required=True)
    parser.add_argument("--curves-raw", type=Path, required=True)
    parser.add_argument("--curves-sha256", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    require(not args.output_dir.exists(), "Use a new output directory")
    require(file_sha(game / "game_dx12_ship_replay.exe") == REPLAY_SHA256, "Replay executable differs")
    original = COMMON_RAW.read_bytes()
    require(sha(original) == COMMON_RAW_SHA256 and file_sha(COMMON_TRACE) == COMMON_TRACE_SHA256,
            "Reviewed common-stream raw or native trace differs")
    trace = read_json(COMMON_TRACE)
    complete(trace, len(original), 28)
    old_names = [(a["type"], a.get("name"), a["root"]) for a in trace["assets"]]
    root_reads = [r for r in trace["reads"] if r["physical"] == ROOT_TABLE_AT]
    require(len(root_reads) == 1 and root_reads[0]["size"] == ROOT_TABLE_BYTES and
            root_reads[0]["block"] == 5 and root_reads[0]["offset"] == 1494304,
            "Native original root-table allocation differs")
    require(len(trace["refs"]) == 3 and struct.unpack_from("<I", original)[0] == 33212,
            "Original script-string or packed-reference inventory differs")
    unchanged, _ = rebuild(original, trace)
    require(unchanged == original, "No-edit native record reconstruction changed bytes")
    installed = game / "magma/maps/mp_sm_island_1/magma_island_common_stream.ff"
    require(file_sha(installed) == COMMON_FF_SHA256 and decode(installed) == original,
            "Installed common-stream fastfile differs from reviewed source")

    image_receipt = read_json(args.images_dir / "images.json")
    image_trace = read_json(args.images_dir / "load.trace.json")
    images = (args.images_dir / "vehicle-images.raw").read_bytes()
    require(sha(images) == IMAGE_RAW_SHA256 == image_receipt["raw_sha256"] == image_trace["payload_sha256"],
            "Verified image raw differs")
    complete(image_trace, len(images), 14)
    require(not image_trace["refs"] and len(image_receipt["images"]) == 14,
            "New images must not retain external packed references")
    image_names = [r["name"] for r in image_receipt["images"]]
    image_body = root_body(images, [19] * 14)

    curves = args.curves_raw.read_bytes()
    require(sha(curves) == args.curves_sha256.lower(), "Verified curve/NCS raw differs")
    curve_body = root_body(curves, [61, 87, 87, 87])
    curve_reader = ReplayArchive(curves)
    curve_report = curve_reader.run()
    complete(curve_report, len(curves), 4)
    require(not curve_report["refs"] and [(a["type"], a["name"]) for a in curve_reader.assets] ==
            list(zip([61, 87, 87, 87], CURVE_NAMES)), "Curve/NCS identity or reference closure differs")
    del curve_reader, curve_report
    gc.collect()

    kinds = [19] * 14 + [61, 87, 87, 87]
    entries = b"".join(struct.pack("<IIQ", kind, 0, INLINE) for kind in kinds)
    old_table = original[ROOT_TABLE_AT:ROOT_TABLE_AT + ROOT_TABLE_BYTES]
    relocated, _ = rebuild(original, trace, {ROOT_TABLE_AT: (old_table + entries, None)})
    prefix = bytearray(relocated)
    struct.pack_into("<I", prefix, 16, 46)
    require(len(prefix) == 2144778 and sha(prefix) == RELOCATED_PREFIX_SHA256,
            "Relocated original prefix differs from independent reference audit")
    candidate = bytes(prefix) + image_body + curve_body
    added_names = [(kind, name, 28 + index) for index, (kind, name) in
                   enumerate(zip(kinds, image_names + list(CURVE_NAMES)))]
    output = args.output_dir
    output.mkdir(parents=True)
    (output / "magma_island_common_stream.raw").write_bytes(candidate)
    phases = {phase: walk(reader, candidate, output, phase, old_names, added_names)
              for phase, reader in (("load", ReplayArchive), ("preload", PreloadArchive))}
    maxima = [max(a, b) for a, b in zip(phases["load"]["maximum"], phases["preload"]["maximum"])]
    packed = bytearray(custom_container(installed.read_bytes()[:136], candidate, maxima,
                                       phases["preload"]["preload_walk_size"]))
    struct.pack_into("<I", packed, 12, 4087)
    destination = output / "magma_island_common_stream.ff"
    destination.write_bytes(packed)
    require(decode(destination) == candidate, "Packaged common-stream native decode differs")
    write_json(output / "assembly.json", {
        "status": "COMMON_STREAM_46_ROOTS_NATIVE_LOAD_PRELOAD_VERIFIED",
        "baseline_ff_sha256": COMMON_FF_SHA256, "baseline_raw_sha256": COMMON_RAW_SHA256,
        "baseline_load_trace_sha256": COMMON_TRACE_SHA256,
        "relocated_prefix_sha256": RELOCATED_PREFIX_SHA256,
        "image_raw_sha256": IMAGE_RAW_SHA256, "curve_ncs_raw_sha256": sha(curves),
        "original_roots_preserved": 28, "original_script_strings_preserved": 33212,
        "root_table_bytes_added": len(entries), "packed_references_relocated_from_native_trace": 3,
        "new_roots": added_names, "raw_bytes": len(candidate), "raw_sha256": sha(candidate),
        "fastfile": str(destination), "fastfile_bytes": len(packed), "fastfile_sha256": sha(packed),
        "phases": phases, "installed_files_modified": False, "live_rendering_verified": False})
    print("COMMON_STREAM_READY", len(packed), sha(packed), str(destination), flush=True)


if __name__ == "__main__":
    main()
