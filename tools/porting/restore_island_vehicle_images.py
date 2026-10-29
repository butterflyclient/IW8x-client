"""Stage the fourteen missing Island vehicle images and their original pixels.

The selected owners come from complete native Source 1.64 traversals. Only
the image format enum is translated using the paired native DXGI proof.
Replay LOAD/PRELOAD must consume the exact isolated archive. The cache phase
preserves every installed common-cache packet and adds authenticated source
packets in a new file; neither phase writes an installed game file.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import gc
import hashlib
import json
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
from geometry import XpakIndex, Oodle, decode_packet
from content_hash import verify_content_key
from streamed_images import image_parts, include_images, _index


INVENTORY_SHA256 = "621caf98e9796165ee6583e733511be2b71357e0953a7f9baab29c5dcc44a60c"
FORMATS_SHA256 = "e42427796e38921af193f58103f283ba3708d123143cf29847934196523f5aad"
REPLAY_SHA256 = "68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0"
BASE_CACHE_SHA256 = "036a7d995186370da1ed3438d2cbab06a7622dd4ea1b11233017de278c48d7cd"
SOURCE_GAME = Path(r"D:\Games\Call of Duty Modern Warfare 1.64.5.13113888 (WZ ONLY) Uploaded by RyGuy2Cool"
                   r"\Call of Duty Modern Warfare 1.64 (WZ ONLY)")
INLINE = 0xfffffffffffffffe
FORMAT_MAP = {36: 34, 46: 44, 47: 45}


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


def selected_trace(path, selected):
    """Read the native pretty-JSON trace without expanding its unrelated roots.

    The global stream trace is 820 MiB. Its reads/refs/assets are flat record
    arrays with a fixed indentation emitted by the retained native reader.
    Decode each record independently and retain only the selected owners.
    """
    roots = {asset["root"] for asset in selected}
    spans = [(asset["physical"], asset["end"]) for asset in selected]
    result = {"reads": [], "refs": [], "assets": []}
    with path.open(encoding="utf-8-sig") as stream:
        for line in stream:
            match = re.match(r'^  "([a-z_0-9]+)": (.*)', line)
            if not match:
                continue
            field, value = match.groups()
            if value == "[":
                keep = field in ("reads", "refs", "assets")
                record = []
                closed = False
                for child in stream:
                    if child.rstrip("\r\n") in ("  ],", "  ]"):
                        require(not record, "Native trace array ended inside a record")
                        closed = True
                        break
                    if not keep:
                        continue
                    if not record:
                        require(child.rstrip("\r\n") == "    {", "Native trace record layout differs")
                    record.append(child)
                    require(sum(map(len, record)) < 131072, "Native trace record exceeds expected bound")
                    if child.rstrip("\r\n") in ("    },", "    }"):
                        item = json.loads("".join(record).rstrip().rstrip(","))
                        record = []
                        if ((field == "reads" and item["asset"] in roots) or
                                (field == "assets" and item["root"] in roots) or
                                (field == "refs" and any(a <= item["physical"] < b for a, b in spans))):
                            result[field].append(item)
                require(closed, "Native trace array is truncated")
            elif field in ("version", "complete", "error", "count", "processed", "physical_size", "input_sha256"):
                result[field] = json.loads(value.rstrip(","))
    return result


def native_walk(reader, raw, output, phase, names):
    archive = reader(raw)
    try:
        result = archive.run()
    except Exception as error:
        archive.error = str(error)
        result = archive.report()
    result["payload_sha256"] = sha(raw)
    if phase == "load":
        result["assets"] = archive.assets
    write_json(output / (phase + ".trace.json"), result)
    require(result["complete"] and not result.get("error") and
            result["count"] == result["processed"] == len(names) and
            result["physical_size"] == len(raw), "Replay " + phase + " did not reach exact EOF")
    if phase == "load":
        require([(a["type"], a["name"]) for a in archive.assets] ==
                [(19, name) for name in names], "Replay image root registration differs")
    summary = {key: result[key] for key in ("count", "processed", "complete", "error",
               "physical_size", "maximum", "preload_walk_size") if key in result}
    del archive, result
    gc.collect()
    print(json.dumps({phase: summary}), flush=True)
    return summary


def extract(args):
    require(not args.output_dir.exists(), "Use a new staging directory")
    require(file_sha(args.inventory) == INVENTORY_SHA256, "Reviewed image ownership inventory differs")
    inventory = read_json(args.inventory)
    names = set(inventory["missing_names"])
    require(len(names) == 14 and len(inventory["owners"]) == 14, "Expected fourteen exact image owners")
    proof_path = EVIDENCE / "pipeline120/paired-native-image-formats.json"
    require(file_sha(proof_path) == FORMATS_SHA256, "Paired native format proof differs")
    proof = read_json(proof_path)
    require(proof["complete"] and proof["original_fault_reproduced"], "Incomplete native image format proof")
    pairs = {p["source_format"]: p for p in proof["pairs"] if p["source_format"] in FORMAT_MAP}
    require(set(pairs) == set(FORMAT_MAP), "Native format mapping is incomplete")
    for value, pair in pairs.items():
        require(pair["target_format"] == FORMAT_MAP[value] and pair["native_semantics_equal"] and
                pair["source_dxgi"] == pair["target_dxgi"] and pair["target_bytes_per_block"] in (8, 16),
                "Source and Replay pixel formats are not equivalent")

    payloads = {}
    source_reports = []
    for owner in inventory["traces"]:
        source = Path(owner["input"])
        raw = source.read_bytes()
        require(sha(raw) == owner["input_sha256"], "Source decoded archive hash differs")
        trace_path = Path(owner["trace"])
        selected = [row["asset"] for row in inventory["owners"] if row["trace"] == str(trace_path)]
        trace = selected_trace(trace_path, selected)
        require(trace["version"] == "164" and trace["complete"] and not trace.get("error") and
                trace["count"] == trace["processed"] and trace["physical_size"] == len(raw) and
                trace["input_sha256"] == sha(raw), "Source owner lacks complete exact native traversal")
        selected_roots = {row["root"] for row in selected}
        reads = defaultdict(list)
        for entry in trace["reads"]:
            if entry["asset"] in selected_roots:
                reads[entry["asset"]].append(entry)
        registrations = {a["root"]: a for a in trace["assets"] if a["type"] == 19 and a.get("name") in names}
        for asset in selected:
            name = asset["name"]
            require(name not in payloads and registrations.get(asset["root"]) == asset,
                    "Source image identity or unique ownership differs: " + name)
            begin, end = asset["physical"], asset["end"]
            header = raw[begin:begin + 232]
            require(asset["size"] == 232 and header.hex() == asset["serialized_header"] and
                    sha(header) == asset["header_sha256"] and 32 <= begin < end <= len(raw),
                    "Source image extent/header differs: " + name)
            native_reads = reads[asset["root"]]
            require(len(native_reads) == 4, "Unverified source image child read count: " + name)
            cursor = begin
            for entry in native_reads:
                require(entry["block"] != 4 and entry["physical"] == cursor,
                        "Noncontiguous or runtime image payload: " + name)
                cursor += entry["size"]
            require(cursor == end and not any(begin <= ref["physical"] < end for ref in trace["refs"]),
                    "Image contains unaccounted bytes or unresolved packed references: " + name)
            require(struct.unpack_from("<Q", header)[0] == INLINE, "Image name must be inline")
            source_format = struct.unpack_from("<I", header, 20)[0]
            require(source_format in pairs, "Unverified image format: " + str(source_format))
            original = raw[begin:end]
            converted = bytearray(original)
            struct.pack_into("<I", converted, 20, FORMAT_MAP[source_format])
            require(converted[:20] == original[:20] and converted[24:] == original[24:],
                    "Image conversion changed bytes outside the format enum")
            payloads[name] = (bytes(converted), {
                "name": name, "type": 19, "source_zone": source.stem,
                "source_root": asset["root"], "source_physical": begin,
                "source_payload_sha256": sha(original), "payload_bytes": len(converted),
                "payload_sha256": sha(converted), "source_format": source_format,
                "replay_format": FORMAT_MAP[source_format], "dxgi_format": pairs[source_format]["source_dxgi"],
                "native_reads": len(native_reads), "unresolved_packed_references": 0})
        source_reports.append({"raw": str(source), "raw_sha256": sha(raw), "trace": str(trace_path),
                               "trace_sha256": file_sha(trace_path), "count": trace["count"], "complete": True})
        print("SOURCE_OWNER_VERIFIED", source.stem, len(selected), flush=True)
        del trace, raw, registrations, reads
        gc.collect()
    require(set(payloads) == names, "Missing exact source image owners")
    ordered = sorted(payloads)
    output = args.output_dir
    output.mkdir(parents=True)
    records = []
    for number, name in enumerate(ordered):
        payload, record = payloads[name]
        record["payload_file"] = f"image-{number:02d}.bin"
        (output / record["payload_file"]).write_bytes(payload)
        records.append(record)
    raw = struct.pack("<4Q", 0, 0, len(ordered), INLINE)
    raw += b"".join(struct.pack("<2Q", 19, INLINE) for _ in ordered)
    raw += b"".join(payloads[name][0] for name in ordered)
    (output / "vehicle-images.raw").write_bytes(raw)
    phases = {phase: native_walk(reader, raw, output, phase, ordered)
              for phase, reader in (("load", ReplayArchive), ("preload", PreloadArchive))}
    write_json(output / "images.json", {
        "status": "EXACT_SOURCE_IMAGES_REPLAY_FORMATS_LOAD_PRELOAD_VERIFIED",
        "inventory_sha256": INVENTORY_SHA256, "native_formats_proof_sha256": FORMATS_SHA256,
        "raw_sha256": sha(raw), "raw_bytes": len(raw), "images": records, "source_owners": source_reports,
        "phases": phases, "only_image_format_enum_changed": True,
        "installed_files_modified": False, "live_rendering_verified": False})
    print("IMAGE_ROOTS_READY", len(records), sha(raw), str(output), flush=True)


def cache(args):
    output = args.output_dir
    receipt = read_json(output / "images.json")
    require(receipt["status"] == "EXACT_SOURCE_IMAGES_REPLAY_FORMATS_LOAD_PRELOAD_VERIFIED" and
            receipt["inventory_sha256"] == INVENTORY_SHA256 and len(receipt["images"]) == 14,
            "Image extraction is not verified")
    raw = (output / "vehicle-images.raw").read_bytes()
    require(sha(raw) == receipt["raw_sha256"], "Staged image archive differs")
    images = []
    cursor = 32 + len(receipt["images"]) * 16
    for record in receipt["images"]:
        payload = (output / record["payload_file"]).read_bytes()
        require(sha(payload) == record["payload_sha256"] and raw[cursor:cursor + len(payload)] == payload,
                "Staged image root differs")
        images.append({"name": record["name"], "physical": cursor, "imported": False})
        cursor += len(payload)
    require(cursor == len(raw), "Unconsumed isolated image archive bytes")
    parts = image_parts(raw, images)
    require(len(parts) == 56, "Expected four exact source stream parts per image")
    unique = {}
    for part in parts:
        require(part["key"] not in unique or unique[part["key"]]["bytes"] == part["bytes"],
                "One source key has conflicting decoded sizes")
        unique.setdefault(part["key"], part)
    target = output / "cache_magma_island164_common.xpak"
    require(not target.exists(), "Refusing to overwrite an existing staged cache")
    require(file_sha(args.base_cache) == BASE_CACHE_SHA256, "Installed common cache differs from reviewed baseline")
    files = sorted((args.source_game / "zone").glob("*.xpak"))
    require(len(files) >= 200, "Original Source XPAK installation is incomplete")
    print("INDEXING_SOURCE_XPAKS", len(files), "required_keys", len(unique), flush=True)
    index = XpakIndex(files, keys=set(unique))
    missing = set(unique) - set(index.entries)
    if missing:
        extra = XpakIndex(sorted((args.source_game / "xpak_cache").glob("*.xpak")), keys=missing)
        index.entries.update(extra.entries)
    for packet_pack in args.packet_pack:
        missing = set(unique) - set(index.entries)
        if not missing:
            break
        require(packet_pack.is_file(), "Additional exact-content packet archive is absent")
        extra = XpakIndex([packet_pack], keys=missing)
        index.entries.update(extra.entries)
    missing = set(unique) - set(index.entries)
    if missing:
        write_json(output / "source-image-packets-missing.json", {
            "source_game": str(args.source_game), "source_zone_archives_checked": len(files),
            "missing": [{**unique[key], "key": f"{key:016X}"} for key in sorted(missing)],
            "found": [{"key": f"{key:016X}", "owners": [(str(p), at, size) for p, at, size in owners]}
                      for key, owners in sorted(index.entries.items())],
            "installed_files_modified": False})
        print("MISSING_SOURCE_IMAGE_PACKETS", len(missing),
              json.dumps([{**unique[key], "key": f"{key:016X}"} for key in sorted(missing)]), flush=True)
    require(set(unique) <= set(index.entries), "Original source image XPAK packets are missing")
    codec_path = args.source_game / "oo2core_7_win64.dll"
    codec_sha256 = file_sha(codec_path)
    codec = Oodle(codec_path)
    packets = []
    for key, part in sorted(unique.items()):
        packet = index.read(key)
        pixels = decode_packet(packet, part["bytes"], codec)
        verify_content_key(key, pixels)
        owner, offset, extent = index.entries[key][0]
        require(len(packet) == extent, "Source image packet was truncated")
        packets.append({"key": f"{key:016X}", "name": part["name"], "part": part["part"],
                        "pixel_bytes": len(pixels), "pixel_sha256": sha(pixels),
                        "source_xpak": str(owner), "packet_offset": offset,
                        "packet_bytes": extent, "packet_sha256": sha(packet)})
    write_json(output / "source-image-packets.json", {"source_codec_sha256": codec_sha256,
               "authenticated_source_indexes": len(files), "additional_packet_archives": list(map(str, args.packet_pack)),
               "all_content_keys_verified": True, "packets": packets})
    report = include_images(raw, images, index, codec, args.base_cache, target)
    original = args.base_cache.read_bytes()
    staged = target.read_bytes()
    old_entries, old_base, old_extent = _index(original)
    new_entries, new_base, _ = _index(staged)
    require(staged[new_base:new_base + old_extent] == original[old_base:old_base + old_extent] and
            all(new_entries.get(key) == value for key, value in old_entries.items()),
            "Existing common-cache payloads or entries changed")
    require(sha(original) == BASE_CACHE_SHA256 and file_sha(args.base_cache) == BASE_CACHE_SHA256,
            "Installed source cache changed during staging")
    write_json(output / "cache.json", {"status": "EXACT_SOURCE_PIXELS_MERGED_OFFLINE",
               "base_cache_sha256": BASE_CACHE_SHA256, "existing_entries_preserved": len(old_entries),
               "existing_payload_bytes_preserved": old_extent, "candidate": str(target),
               "native_144_cache_checks": report, "native_replay_gpu_creation_verified": False,
               "installed_files_modified": False})
    print("IMAGE_CACHE_READY", report["bytes"], report["sha256"], "parts", len(parts), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("extract", "cache"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--source-game", type=Path, default=SOURCE_GAME)
    parser.add_argument("--packet-pack", type=Path, action="append", default=[],
                        help="Additional native XPAK for exact missing content keys; pixels still require key verification")
    parser.add_argument("--base-cache", type=Path,
                        default=game / "magma/maps/mp_sm_island_1/cache_magma_island164_common.xpak")
    args = parser.parse_args()
    require(file_sha(game / "game_dx12_ship_replay.exe") == REPLAY_SHA256, "Replay executable differs")
    if args.phase == "extract":
        require(args.inventory is not None, "Extraction requires the reviewed --inventory")
        extract(args)
    else:
        cache(args)


if __name__ == "__main__":
    main()
