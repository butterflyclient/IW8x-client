"""Restore omitted Island vehicle scalars from verified Replay stock assets.

This stages a new archive and executes Replay's complete LOAD and PRELOAD
readers. It never writes the installed map, manifest or DLL. The input is the
specific installed serialization probe; its existing reference ownership and
all unrelated bytes are preserved. See docs/reverse-engineering/island_vehicles.md.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import gc
import hashlib
import json
from pathlib import Path
import struct
import sys


PROJECT = Path(__file__).resolve().parents[2]
REPO = PROJECT.parents[1]
EVIDENCE = REPO / "evidence/island-backport-20261013"
sys.path[:0] = [str(EVIDENCE), str(REPO / "tools/fastfiles"),
                str(REPO / "build/fastfile-python")]

from replay_walk import ReplayArchive, decode, game
from preload120.walk_preload import PreloadArchive
from port_archive import custom_container


BASELINE_SHA256 = "be368a0aa50ee199e3bb8880052774c5a255193eb424fbf86c3049d1dbe4fff3"
BASELINE_RAW_SHA256 = "e60b0e5470435988999822e24182035686cd80b7535a4b0dd6381fefe51c6edd"
PROJECTION_SHA256 = "fadf1dda552cfea7ea1f2c9c5d7c54b327aa462e9140261517a5490ef58a90d7"
REPLAY_SHA256 = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0"
VEHICLE_NAMES = ("atango_physics_mp", "decho_physics_mp",
                 "lbravo_physics_mp", "tromeo_physics_mp")
HEADER_SIZE = 3848


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path):
    with path.open(encoding="utf-8-sig") as stream:
        return json.load(stream)


def write_json(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def complete_trace(trace, size):
    require(trace["complete"] and not trace.get("error"), "Native archive traversal is incomplete")
    require(trace["count"] == trace["processed"], "Native archive root count differs")
    require(trace["physical_size"] == size, "Native archive traversal did not consume the exact payload")


def vehicle_assets(trace, data):
    result = {}
    for asset in trace["assets"]:
        if asset["type"] != 59 or asset.get("name") not in VEHICLE_NAMES:
            continue
        name = asset["name"]
        require(name not in result, "Duplicate vehicle owner: " + name)
        require(asset["size"] == HEADER_SIZE, "VehicleDef stride differs: " + name)
        at = asset["physical"]
        header = data[at:at + HEADER_SIZE]
        require(len(header) == HEADER_SIZE and header.hex() == asset["serialized_header"],
                "Serialized header does not match native registration: " + name)
        result[name] = (asset, header)
    require(set(result) == set(VEHICLE_NAMES), "Reference archive lacks an exact named vehicle owner")
    return result


def fields(header):
    return {
        "type": header[8],
        "health": struct.unpack_from("<i", header, 24)[0],
        "num_doors": struct.unpack_from("<i", header, 44)[0],
        "physics_enabled": struct.unpack_from("<i", header, 0xF8)[0],
        "physics_netcode_type": struct.unpack_from("<i", header, 0xFC)[0],
        "physics_game_profile": header[0x100],
        "physics_animation_profile": struct.unpack_from("<I", header, 0x104)[0],
        "wheel_count": struct.unpack_from("<I", header, 0x108)[0],
    }


def native_walk(reader, data, output, label, expected_count):
    archive = reader(data)
    try:
        report = archive.run()
    except Exception as error:
        archive.error = str(error)
        report = archive.report()
    if label == "load":
        report["assets"] = archive.assets
    report["payload_sha256"] = sha(data)
    write_json(output / (label + ".trace.json"), report)
    complete_trace(report, len(data))
    require(report["count"] == expected_count, "Candidate changed the native root count")
    summary = {key: report[key] for key in ("count", "processed", "complete", "error",
               "physical_size", "maximum", "preload_walk_size") if key in report}
    del archive, report
    gc.collect()
    print(json.dumps({label: summary}), flush=True)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installed", type=Path,
                        default=game / "magma/maps/mp_sm_island_1/mp_sm_island_1.ff")
    parser.add_argument("--native-raw", type=Path, required=True)
    parser.add_argument("--native-trace", type=Path, required=True)
    parser.add_argument("--native-sha256", required=True,
                        help="Decoded result SHA-256 from hash/checksum-verified stock FF+FP application")
    parser.add_argument("--native-owners-only", action="store_true",
                        help="Accept an incomplete reference archive only if all four vehicle roots completed; candidate LOAD/PRELOAD must still complete")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    require(not args.output_dir.exists(), "Use a new output directory; existing evidence is immutable")
    require(file_sha(game / "game_dx12_ship_replay.exe") == REPLAY_SHA256, "Replay executable differs")
    packed = args.installed.read_bytes()
    require(sha(packed) == BASELINE_SHA256, "Installed fastfile differs from the reviewed baseline")
    original = decode(args.installed)
    require(sha(original) == BASELINE_RAW_SHA256, "Installed decoded payload differs")
    baseline = read_json(EVIDENCE / "customization-map.load.trace.json")
    complete_trace(baseline, len(original))
    installed = vehicle_assets(baseline, original)
    expected_count = baseline["count"]
    del baseline

    receipt_path = EVIDENCE / "vehicle120/structural-projection-receipt.json"
    require(file_sha(receipt_path) == PROJECTION_SHA256, "Reviewed reference-preservation mask differs")
    receipt = read_json(receipt_path)
    preserved = set()
    for _, at, size in receipt["ranges"]:
        require(0 <= at < at + size <= HEADER_SIZE, "Projection owner leaves VehicleDef")
        preserved.update(range(at, at + size))
    restored = sorted(set(range(HEADER_SIZE)) - preserved)
    require(len(preserved) == 1976 and len(restored) == 1872, "Vehicle projection coverage differs")

    native = args.native_raw.read_bytes()
    require(sha(native) == args.native_sha256.lower(), "Verified stock payload SHA-256 differs")
    trace = read_json(args.native_trace)
    require(str(trace.get("version")) == "120", "Native reference used a different engine reader")
    require(trace.get("phase") == "load", "Native reference is not a completed-owner LOAD trace")
    require(trace.get("decoded_size") == len(native), "Native reference decoded size differs")
    # The stock extractor calls the same decoded-payload digest input_sha256.
    trace_sha256 = trace.get("payload_sha256", trace.get("input_sha256"))
    require(trace_sha256 == sha(native), "Native trace is not tied to the supplied payload")
    reference = vehicle_assets(trace, native)
    if args.native_owners_only and not trace["complete"]:
        require(0 <= trace["processed"] < trace["count"], "Invalid incomplete native root count")
        require(0 <= trace["physical_size"] <= len(native), "Native reference cursor leaves its payload")
        require(bool(trace.get("error")), "Incomplete native reference lacks a recorded failure")
        for name, (asset, _) in reference.items():
            require(0 <= asset["root"] < trace["processed"],
                    "Native vehicle root did not finish: " + name)
            require(0 <= asset["physical"] and
                    asset["physical"] + HEADER_SIZE <= asset["end"] <= trace["physical_size"],
                    "Native vehicle owner was not completely consumed: " + name)
    else:
        complete_trace(trace, len(native))
    native_validation = dict(mode="completed_vehicle_owners" if args.native_owners_only else "complete_archive",
                             complete=trace["complete"], count=trace["count"], processed=trace["processed"],
                             physical_size=trace["physical_size"], error=trace.get("error"),
                             vehicle_roots={name: asset["root"] for name, (asset, _) in reference.items()})
    candidate = bytearray(original)
    records = []
    for name in VEHICLE_NAMES:
        old_asset, old_header = installed[name]
        native_asset, native_header = reference[name]
        require(not any(old_header[i] for i in restored), "An omitted scalar is already populated: " + name)
        values = fields(native_header)
        require(values["physics_enabled"] == 1 and values["health"] > 0,
                "Native reference lacks working physics/health: " + name)
        require(0 <= values["num_doors"] <= 4 and values["wheel_count"] <= 12,
                "Native scalar count exceeds the preserved fixed owner: " + name)
        header = bytearray(old_header)
        for i in restored:
            header[i] = native_header[i]
        require(all(header[i] == old_header[i] for i in preserved), "An owned reference changed")
        at = old_asset["physical"]
        candidate[at:at + HEADER_SIZE] = header
        changed = [i for i in range(HEADER_SIZE) if header[i] != old_header[i]]
        require(changed and set(changed).issubset(restored), "Vehicle edit escaped scalar mask")
        records.append(dict(name=name, root=old_asset["root"], physical=at,
                            native_root=native_asset["root"], native_physical=native_asset["physical"],
                            native_end=native_asset["end"],
                            original_sha256=sha(old_header), native_sha256=sha(native_header),
                            candidate_sha256=sha(header), before=fields(old_header), after=fields(header),
                            changed_header_offsets=changed))
    last = 0
    for record in sorted(records, key=lambda item: item["physical"]):
        at = record["physical"]
        require(candidate[last:at] == original[last:at], "An unrelated archive region changed")
        last = at + HEADER_SIZE
    require(candidate[last:] == original[last:], "The archive suffix changed")
    require(len(candidate) == len(original), "Archive size changed")
    del native, trace, reference, original, installed
    candidate = bytes(candidate)
    gc.collect()

    args.output_dir.mkdir(parents=True)
    raw_path = args.output_dir / "mp_sm_island_1.vehicle-scalars.raw"
    raw_path.write_bytes(candidate)
    load = native_walk(ReplayArchive, candidate, args.output_dir, "load", expected_count)
    preload = native_walk(PreloadArchive, candidate, args.output_dir, "preload", expected_count)
    original_maxima = struct.unpack_from("<8Q", packed, 0x30)
    maxima = [max(a, b, c) for a, b, c in zip(original_maxima, load["maximum"], preload["maximum"])]
    walk_size = max(struct.unpack_from("<Q", packed, 0x28)[0], preload["preload_walk_size"])
    container = bytearray(custom_container(packed[:136], candidate, maxima, walk_size))
    struct.pack_into("<I", container, 12, 4087)
    output = args.output_dir / "mp_sm_island_1.ff"
    output.write_bytes(container)
    require(decode(output) == candidate, "Packaged candidate did not round-trip")
    report = dict(created_utc=datetime.now(timezone.utc).isoformat(),
                  scope="Four exact-name Replay scalar restorations; original typed ownership retained",
                  installed=str(args.installed), installed_sha256=BASELINE_SHA256,
                  native_raw=str(args.native_raw), native_sha256=args.native_sha256.lower(),
                  native_trace=str(args.native_trace), native_trace_sha256=file_sha(args.native_trace),
                  native_reference_validation=native_validation,
                  candidate=str(output), candidate_sha256=sha(container), candidate_bytes=len(container),
                  decoded_sha256=sha(candidate), decoded_bytes=len(candidate),
                  restored_mask_bytes=1872, preserved_header_bytes=1976, vehicles=records,
                  decoded_byte_changes=sum(len(item["changed_header_offsets"]) for item in records),
                  load=load, preload=preload, live_physics_verified=False,
                  client_rendering_verified=False, vehicle_animation_verified=False)
    write_json(args.output_dir / "restoration.json", report)
    print(json.dumps({key: value for key, value in report.items() if key != "vehicles"}, indent=2))


if __name__ == "__main__":
    main()
