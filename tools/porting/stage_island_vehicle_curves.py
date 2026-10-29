"""Stage the missing Island SUV curves and their exact common BR NCS owner.

The output is a four-root Replay archive and independently hashed root payloads
for the existing magma_island_common_stream companion. Nothing is installed.
Stock Replay supplies the curve data; Source's identical curves corroborate it
and its separate common NCS owner supplies only the same three names.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import gc
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct
import sys


PROJECT = Path(__file__).resolve().parents[2]
REPO = PROJECT.parents[1]
EVIDENCE = REPO / "evidence/island-backport-20261013"
sys.path[:0] = [str(EVIDENCE), str(REPO / "tools/fastfiles"),
                str(REPO / "build/fastfile-python")]

from replay_walk import ReplayArchive, game
from preload120.walk_preload import PreloadArchive


INLINE = 0xFFFFFFFFFFFFFFFE
NCS_NAME = "ncs_xac_common_br_mp"
CURVE_NAMES = tuple("iw8_mp_veh_decho_door_" + suffix for suffix in
                    ("back_left", "front_left", "front_right"))
NATIVE_RAW_SHA256 = "1cb413a30b8c4f79efdd300a287d610794a710d832cc02a36d5e4103c6ba3448"
NATIVE_TRACE_SHA256 = "c1c0e3c082d55a9a85ee27ee015ac25298397113f1af7e66a4b0b0188b010ee7"
SOURCE_RAW_SHA256 = "325222c93b90bb4bf2b0cc0f3bfd29d8fc4fbc98e0afca84a038727fbca4f446"
SOURCE_TRACE_SHA256 = "174a7b3bfb1eb6016c96b6030d376c6ba1cd7e365de41115b348499cb69dc481"
REPLAY_SHA256 = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0"
PAYLOAD_SHA256 = "270b5226e8c473e8efe89c62a5bc5f9b4043b3c66489c9c77ccfa0a30eae85a7"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


@contextmanager
def verified_view(path, expected):
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
        require(sha(mapped) == expected, "Pinned input differs: " + str(path))
        yield mapped
        require(sha(mapped) == expected, "Input changed during extraction: " + str(path))


def trace_records(mapped, array, field, values):
    # These immutable traces use the generated reader's flat, indented records.
    # Filtering before decoding avoids retaining a 391 MiB stock trace in RAM.
    marker = ('  "' + array + '": [').encode()
    start = mapped.find(marker)
    require(start >= 0, "Trace array is absent: " + array)
    if mapped[start + len(marker):start + len(marker) + 1] == b"]":
        return []
    end = mapped.find(b"\n  ]", start)
    require(end > start, "Trace array is unterminated: " + array)
    pattern = re.compile(('"' + field + '": (' + "|".join(str(value) for value in values) +
                          r')(?=,?\r?\n)').encode())
    result = []
    for match in pattern.finditer(mapped, start, end):
        begin = mapped.rfind(b"\n    {", start, match.start()) + 1
        finish = mapped.find(b"\n    }", match.end(), end)
        require(start < begin < match.start() < finish < end,
                "Selected native record is not a bounded flat object")
        record = json.loads(mapped[begin:finish + 6])
        require(record[field] in values, "Trace record selector differs")
        result.append(record)
    return result


def capture_trace(path, expected_hash, version, raw_hash, roots):
    with verified_view(path, expected_hash) as mapped:
        start = mapped.find(b'  "reads": [')
        require(start > 0, "Trace metadata is absent")
        metadata = json.loads(mapped[:start] + b'"selected_records": true}')
        digests = re.findall(rb'"(?:input_sha256|payload_sha256)": "([0-9a-f]{64})"', mapped)
        require(digests and all(value.decode() == raw_hash for value in digests),
                "Trace is not bound to the supplied raw payload")
        require(str(metadata["version"]) == version and metadata["phase"] == "load",
                "Wrong native reader profile")
        selected = {key: trace_records(mapped, key, "root" if key == "assets" else "asset", roots)
                    for key in ("assets", "reads", "refs")}
    require(len(selected["assets"]) == len(roots), "A selected owner is missing or has nested assets")
    require({asset["root"] for asset in selected["assets"]} == set(roots), "Selected owner identity differs")
    return metadata, selected


def asset_header(raw, metadata, asset):
    at, end = asset["physical"], asset["end"]
    require(asset["size"] == 32 and 0 <= asset["root"] < metadata["processed"],
            "Selected asset is not a completed 32-byte owner")
    require(0 <= at < at + 32 <= end <= metadata["physical_size"] <= len(raw),
            "Selected native owner leaves the completed archive prefix")
    header = bytes(raw[at:at + 32])
    require(header.hex() == asset["serialized_header"], "Native registered header differs")
    return header


def root_archive(roots):
    require(roots and len({(root["type"], root["name"]) for root in roots}) == len(roots),
            "Duplicate or empty staged root list")
    header = struct.pack("<IIQIIQ", 0, 0, 0, len(roots), 0, INLINE)
    table = b"".join(struct.pack("<QQ", root["type"], INLINE) for root in roots)
    return header + table + b"".join(root["payload"] for root in roots)


def curve_roots(native_raw, native_trace, source_raw, source_trace):
    native_meta, native = capture_trace(native_trace, NATIVE_TRACE_SHA256, "120",
                                        NATIVE_RAW_SHA256, (39, 42241, 42242, 42243))
    source_meta, source = capture_trace(source_trace, SOURCE_TRACE_SHA256, "164",
                                        SOURCE_RAW_SHA256, (19, 4593, 4594, 4595))
    require(source_meta["complete"] and not source_meta["error"] and
            source_meta["count"] == source_meta["processed"] == 7586 and
            source_meta["physical_size"] == source_meta["decoded_size"] == 134473611,
            "Source common reference is not a complete native traversal")
    require(not native_meta["complete"] and native_meta["error"] == "loader did not return" and
            native_meta["count"] == 91844 and native_meta["processed"] == native_meta["failing_asset"] == 91842 and
            native_meta["physical_size"] == 567038599 and native_meta["decoded_size"] == 573432600,
            "Stock reference no longer matches the reviewed completed-owner scope")
    sources = {asset["name"]: asset for asset in source["assets"]}
    natives = {asset["name"]: asset for asset in native["assets"]}
    require(set(sources) == {NCS_NAME, *CURVE_NAMES} and
            set(natives) == {"ncs_xac_level", *CURVE_NAMES}, "Selected names differ")
    roots = []
    with verified_view(native_raw, NATIVE_RAW_SHA256) as stock, verified_view(source_raw, SOURCE_RAW_SHA256) as newer:
        require(len(stock) == native_meta["decoded_size"] and len(newer) == source_meta["decoded_size"],
                "Raw payload extent differs from its native reader")
        ncs = sources[NCS_NAME]
        ncs_header = asset_header(newer, source_meta, ncs)
        expected_header = struct.pack("<QIIIIQ", INLINE, 28, 1, 0, 3, INLINE)
        require(ncs["type"] == 61 and ncs_header == expected_header,
                "Common curve NCS type, source, flags or count differs")
        ncs_payload = bytes(newer[ncs["physical"]:ncs["end"]])
        expected_ncs = expected_header + NCS_NAME.encode() + b"\0" + b"\xff" * 24
        expected_ncs += b"".join(name.encode() + b"\0" for name in CURVE_NAMES)
        require(ncs_payload == expected_ncs and not any(row["asset"] == ncs["root"] for row in source["refs"]),
                "Common NCS is not the exact self-contained three-name owner")
        roots.append(dict(type=61, name=NCS_NAME, payload=ncs_payload, source_asset=ncs,
                          byte_changes=[], string_type=28, source_type=1, flags=0, entries=list(CURVE_NAMES)))

        native_ncs = natives["ncs_xac_level"]
        require(native_ncs["type"] == 61 and
                asset_header(stock, native_meta, native_ncs) == struct.pack("<QIIIIQ", INLINE, 28, 3, 0, 7, INLINE),
                "Replay NCS does not corroborate the curve string type")
        name_reads = [row for row in native["reads"] if row["asset"] == native_ncs["root"] and row["caller"] == 0x11B2B00]
        for index, name in enumerate(CURVE_NAMES):
            asset, source_asset = natives[name], sources[name]
            header = asset_header(stock, native_meta, asset)
            source_header = asset_header(newer, source_meta, source_asset)
            require(asset["type"] == source_asset["type"] == 87 and struct.unpack_from("<Q", header, 8)[0] == INLINE,
                    "Curve is not an inline Replay XAnimCurve owner")
            count = struct.unpack_from("<I", header, 16)[0]
            require(count == (11, 11, 10)[index], "Curve control-point count differs")
            points = [row for row in native["reads"] if row["asset"] == asset["root"] and
                      row["physical"] == asset["physical"] + 32 and row["size"] == count * 12 and
                      row["caller"] == 0xDFEEC3 and row["block"] == 5 and row["offset"] % 4 == 0]
            require(len(points) == 1 and asset["end"] == points[0]["physical"] + points[0]["size"],
                    "Curve points lack their exact generated-loader allocation")
            refs = [row for row in native["refs"] if row["asset"] == asset["root"]]
            require(len(refs) == 1 and refs[0]["physical"] == asset["physical"] and not refs[0]["indirect"] and
                    refs[0]["value"] == struct.unpack_from("<Q", header)[0], "Unexpected curve packed ownership")
            names = [row for row in name_reads if row["block"] == refs[0]["block"] and row["offset"] == refs[0]["offset"]]
            require(len(names) == 1 and bytes(stock[names[0]["physical"]:names[0]["physical"] + names[0]["size"]]) == name.encode() + b"\0",
                    "Curve name does not resolve to its native NCS owner")
            point_bytes = bytes(stock[points[0]["physical"]:asset["end"]])
            require(source_header[8:] == header[8:] and
                    bytes(newer[source_asset["physical"] + 32:source_asset["end"]]) == point_bytes,
                    "Source and Replay curve semantics differ")
            payload = struct.pack("<Q", INLINE) + header[8:] + name.encode() + b"\0" + point_bytes
            roots.append(dict(type=87, name=name, payload=payload, native_asset=asset, source_asset=source_asset,
                              point_read=points[0], native_name_read=names[0], native_name_reference=refs[0],
                              original_header_sha256=sha(header), control_points_sha256=sha(point_bytes),
                              control_point_count=count, curve_type=header[20],
                              duration=struct.unpack_from("<f", header, 24)[0],
                              changed_header_offsets=[i for i in range(8) if header[i] != payload[i]],
                              source_replay_curve_bytes_equal=True))
    proof = dict(native_reference_scope="completed_curve_and_ncs_owners_only", native_traversal=native_meta,
                 source_traversal=source_meta, existing_archives_modified=False,
                 native_inputs=dict(raw=str(native_raw), raw_sha256=NATIVE_RAW_SHA256,
                                    trace=str(native_trace), trace_sha256=NATIVE_TRACE_SHA256),
                 source_inputs=dict(raw=str(source_raw), raw_sha256=SOURCE_RAW_SHA256,
                                    trace=str(source_trace), trace_sha256=SOURCE_TRACE_SHA256))
    return roots, proof


def native_walk(reader, raw, output_dir, label, roots):
    archive = reader(raw)
    try:
        report = archive.run()
    except Exception as error:
        archive.error = str(error)
        report = archive.report()
    report["assets"] = archive.assets
    report["payload_sha256"] = sha(raw)
    with (output_dir / (label + ".trace.json")).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    require(report["complete"] and not report["error"] and report["count"] == report["processed"] == len(roots) and
            report["physical_size"] == report["decoded_size"] == len(raw), "Native " + label + " did not finish the exact payload")
    require(not report["refs"], "Staged roots still refer to an external packed owner")
    if label == "load":
        require([(item["type"], item.get("name"), item["root"]) for item in archive.assets] ==
                [(item["type"], item["name"], index) for index, item in enumerate(roots)],
                "Native registration differs from the intended four roots")
    result = {key: report[key] for key in ("count", "processed", "complete", "error", "physical_size", "decoded_size", "maximum", "preload_walk_size")}
    del archive, report
    gc.collect()
    print(json.dumps({label: result}), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-raw", type=Path, required=True)
    parser.add_argument("--native-trace", type=Path, required=True)
    parser.add_argument("--source-raw", type=Path, required=True)
    parser.add_argument("--source-trace", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    require(output.is_relative_to(PROJECT / "build") and not output.exists(),
            "Use a new staging directory inside this project's build folder")
    require(file_sha(game / "game_dx12_ship_replay.exe") == REPLAY_SHA256, "Pinned Replay executable differs")
    roots, proof = curve_roots(args.native_raw.resolve(), args.native_trace.resolve(),
                               args.source_raw.resolve(), args.source_trace.resolve())
    raw = root_archive(roots)
    require(len(raw) == 851 and sha(raw) == PAYLOAD_SHA256, "Four-root payload differs from the verified native pilot")
    output.mkdir(parents=True)
    (output / "island_vehicle_curves.raw").write_bytes(raw)
    records = []
    for index, root in enumerate(roots):
        path = output / (f"root-{index:02d}-{root['type']}-" + root["name"] + ".bin")
        path.write_bytes(root["payload"])
        records.append({key: value for key, value in root.items() if key != "payload"} |
                       dict(payload_file=str(path), payload_bytes=len(root["payload"]), payload_sha256=sha(root["payload"])))
    phases = {label: native_walk(reader, raw, output, label, roots)
              for label, reader in (("load", ReplayArchive), ("preload", PreloadArchive))}
    receipt = dict(created_utc=datetime.now(timezone.utc).isoformat(), tool_sha256=file_sha(Path(__file__)),
                   scope="Exact common BR curve NCS owner and three byte-identical stock Replay SUV door curves",
                   roots=records, payload=str(output / "island_vehicle_curves.raw"),
                   payload_bytes=len(raw), payload_sha256=sha(raw), phases=phases, reference_proof=proof,
                   unrelated_assets_modified=[], installed_files_modified=[],
                   live_ncs_lookup_verified=False, live_door_animation_verified=False,
                   merge_requirement="Append all four roots to the already scheduled Island common_stream companion; rerun complete LOAD/PRELOAD for the combined archive")
    with (output / "curves.json").open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(receipt, stream, indent=2)
        stream.write("\n")
    print(json.dumps(dict(receipt=str(output / "curves.json"), payload_sha256=sha(raw),
                         roots=[{key: item[key] for key in ("type", "name", "payload_file", "payload_bytes", "payload_sha256")} for item in records]), indent=2))


if __name__ == "__main__":
    main()
