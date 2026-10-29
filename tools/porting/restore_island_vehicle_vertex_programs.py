"""Stage Replay instance transforms in both Island vehicle shader archives.

The converted programs still consume Source's compact quaternion instances.
Replay uploads matrix instances. Matching native vertex programs retain their
instructions; only the NORMAL output name is adapted for the existing pixel
shader. The two SH-lighting variants require a separately audited transform
repair because their native counterparts have a different output interface.

All shader edits use complete native typed reads and the existing packed-pointer
rebuilder. Only pipelines using an edited vertex program receive new identities.
This command writes a new candidate directory; it never deploys or opens a game
process. Native LOAD/PRELOAD completion is serialization proof, not a live test.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import gc
from pathlib import Path
import struct
import zlib

from build_island_vehicle_techsets import (
    NAMES, PROJECT, REPLAY_SHA256, ReplayArchive, PreloadArchive, chunks,
    complete, custom_container, decode, finalize_keys, game, read_json,
    rebuild, require, retail_hash, sha, write_json,
)


BASELINES = {
    'companion': {
        'name': 'techsets_magma_island_common_stream', 'roots': 31,
        'fastfile_sha256': '57f920b9fb542f4140b34fbb224ffe882bf2eab6d5e0a46f3a04c42884f3e950',
        'raw_sha256': '2fd8b6ef532299893f82febae6e94120362ed4c1d240eb310298b0c8174dc0bf',
    },
    'map_techsets': {
        'name': 'techsets_mp_sm_island_1', 'roots': 7314,
        'fastfile_sha256': '0d7a1c8a817d2b3b7749ec34af6f923a69fa7fb913932fc3516f8d624b8a399b',
        'raw_sha256': '14375aadadc18e71269d26658af3e81ebe9a77e78b815f30a2886d4d8dc392f6',
    },
}
CONTRACT_SHA256 = 'd40657dc9dca1c5f9ce60bf00d815d2536a338998d4a3fbbdbc6dd4bd3bae3d3'


def signature(body):
    require(len(body) >= 8, 'Truncated shader signature')
    count, table = struct.unpack_from('<II', body)
    require(table == 8 and 8 + count * 24 <= len(body), 'Unsupported signature table')
    result = []
    for index in range(count):
        at = 8 + index * 24
        name_at, semantic, system, component, register, mask, used, reserved = struct.unpack_from('<5I2BH', body, at)
        end = body.find(b'\0', name_at)
        require(8 + count * 24 <= name_at <= end < len(body), 'Unbounded signature name')
        result.append((body[name_at:end].decode('ascii'), semantic, system, component,
                       register, mask, used, reserved))
    return result


def verified_program(path, expected_sha256, expected_size):
    data = Path(path).read_bytes()
    require(len(data) == expected_size and sha(data) == expected_sha256, 'Shader payload identity differs: ' + str(path))
    parts = chunks(data)
    require(data[4:20] == retail_hash(data[20:]), 'Shader DXBC checksum differs')
    require(len({tag for tag, _ in parts}) == len(parts), 'Duplicate DXBC chunk')
    code = [body for tag, body in parts if tag in (b'SHEX', b'SHDR')]
    require(len(code) == 1 and len(code[0]) >= 8 and struct.unpack_from('<I', code[0])[0] >> 16 == 1,
            'Expected one vertex instruction program')
    return data


def adapt_output_signature(native, current, row):
    before, target = dict(chunks(native)), dict(chunks(current))
    require(signature(before[b'ISGN']) == signature(target[b'ISGN']), 'Native vertex input contract differs')
    actual, expected = signature(before[b'OSGN']), signature(target[b'OSGN'])
    require(len(actual) == len(expected), 'Vertex outputs need a transform repair, not semantic renaming')
    allowed = row['signatures']['allowed_output_semantic_renames']
    changes = []
    body = bytearray(before[b'OSGN'])
    for index, (old, new) in enumerate(zip(actual, expected)):
        require(old[1:] == new[1:], 'Vertex output register/type/mask differs')
        if old[0] == new[0]:
            continue
        require((old[0], new[0]) == ('NORMAL', 'NORMAL_TRIID'), 'Unreviewed output semantic conversion')
        change = dict(parameter=index, register=old[4], index=old[1], native=old[0], current=new[0])
        require(change in allowed, 'Semantic change lacks its exact reviewed contract')
        struct.pack_into('<I', body, 8 + index * 24, len(body))
        body.extend(new[0].encode('ascii') + b'\0')
        while len(body) % 4:
            body.append(0)
        changes.append(change)
    require(changes == allowed, 'Signature contract has unused or reordered conversions')
    require(signature(body) == expected, 'Adapted outputs do not exactly match the current vertex interface')
    if not changes:
        return native, changes
    parts = [(tag, bytes(body) if tag == b'OSGN' else data) for tag, data in chunks(native)]
    result = bytearray(native[:32] + b'\0' * (4 * len(parts)))
    for index, (tag, data) in enumerate(parts):
        struct.pack_into('<I', result, 32 + index * 4, len(result))
        result.extend(tag + struct.pack('<I', len(data)) + data)
    struct.pack_into('<I', result, 24, len(result))
    result[4:20] = retail_hash(result[20:])
    result = bytes(result)
    after = dict(chunks(result))
    require(all(after[tag] == data for tag, data in before.items() if tag != b'OSGN'),
            'Native shader instructions or another chunk changed')
    return result, changes


def load_replacements(contracts, output):
    rows = contracts['source_vertex_programs']
    require(len(rows) == 19 and len({row['current']['name'] for row in rows}) == 19,
            'Expected the complete nineteen-program vehicle closure')
    replacements, proofs = {}, []
    for row in rows:
        source, native = row['current'], row['native']
        require(source['type'] == 14, 'Contract targets a non-vertex shader')
        current = verified_program(source['path'], source['sha256'], source['bytes'])
        original = verified_program(native['path'], native['sha256'], native['bytes'])
        if row['signatures']['signature_only_substitution_eligible']:
            result, renames = adapt_output_signature(original, current, row)
            proof = dict(mode='authenticated_native_instructions', semantic_renames=renames,
                         native_name=native['name'], native_sha256=sha(original), native_instructions_preserved=True)
        else:
            # These variants retain Source's three SH-lighting outputs. Their
            # exact matrix repair is deliberately required before staging.
            from island_vehicle_instance_transforms import rewrite
            result, proof = rewrite(current, source['name'])
            require(signature(dict(chunks(result))[b'OSGN']) == signature(dict(chunks(current))[b'OSGN'])
                    and dict(chunks(result))[b'ISGN'] == dict(chunks(current))[b'ISGN'],
                    'Source lighting interface changed during instance repair')
        require(result != current and retail_hash(result[20:]) == result[4:20], 'Invalid or ineffective vertex repair')
        name = source['name']
        path = output / (name + '.dxbc')
        path.write_bytes(result)
        replacements[name] = dict(before=current, after=result)
        proofs.append(dict(name=name, type=14, before_sha256=sha(current), before_bytes=len(current),
                           sha256=sha(result), bytes=len(result), path=str(path), crc32=hex(zlib.crc32(result)),
                           proof=proof))
    return replacements, proofs


def native_walk(reader, raw, output, label, roots):
    archive = reader(raw)
    try:
        trace = archive.run()
    except Exception as error:
        archive.error = str(error)
        trace = archive.report()
    trace.update(assets=archive.assets, payload_sha256=sha(raw), input_sha256=sha(raw))
    path = output / (label + '.trace.json')
    write_json(path, trace)
    complete(trace, raw)
    require(trace['count'] == roots, 'Native root count changed')
    print(label, roots, 'roots; exact EOF', len(raw), flush=True)
    return archive, trace


def vertex_users(archive, replacements):
    assets = {asset['physical']: asset for asset in archive.assets}
    result = {}
    for technique in archive.techniques:
        pointer = archive.q(technique['resident'] + 88)
        if not pointer:
            continue
        at = archive.locate(pointer)
        asset = assets.get(at)
        require(asset is not None and asset['type'] == 14, 'Technique VS lacks its native registered owner')
        name = asset['name'].lstrip(',')
        if name in replacements:
            require(not asset['name'].startswith(','), 'Selected VS is an unresolved external definition')
            result[technique['physical']] = name
    return result


def pipeline_records(archive):
    result = {}
    for technique in archive.techniques:
        at, resident = technique['physical'], technique['resident']
        header = archive.data[at:at + 184]
        count = header[14] * header[131]
        pointer = archive.q(resident + 40)
        require(pointer or not count, 'Missing native pipeline records')
        if not count:
            continue
        physical = archive.locate(pointer)
        for index in range(count):
            item = physical + 16 * index
            result.setdefault(item, []).append(at)
    return result


def typed_edits(raw, archive, replacements):
    reads = {read['physical']: read for read in archive.reads}
    edits, changed = {}, []
    for asset in archive.assets:
        name = asset['name']
        if asset['type'] != 14 or name not in replacements:
            continue
        at = asset['physical']
        require(asset['size'] == reads[at]['size'] == 40, 'VS header is not a complete native allocation')
        header = raw[at:at + 40]
        size = struct.unpack_from('<I', header, 32)[0]
        candidates = [read for read in archive.reads if read['asset'] == asset['root']
                      and read['size'] == size and at + 40 <= read['physical'] < asset['end']
                      and raw[read['physical']:read['physical'] + 4] == b'DXBC']
        require(len(candidates) == 1, 'VS program lacks an exact inline native allocation')
        program = candidates[0]['physical']
        old, new = replacements[name]['before'], replacements[name]['after']
        require(raw[program:program + size] == old, 'Installed VS differs from the reviewed program: ' + name)
        updated = bytearray(header)
        struct.pack_into('<II', updated, 32, len(new), zlib.crc32(new))
        for physical, data in ((at, bytes(updated)), (program, new)):
            require(physical not in edits or edits[physical] == (data, None), 'Shared VS edits conflict')
            edits[physical] = (data, None)
        changed.append(dict(name=name, header=at, program=program, before_sha256=sha(old), sha256=sha(new)))
    require(changed, 'Archive contains none of the reviewed vertex definitions')
    return edits, changed


def packed_reference_owners(data, trace):
    """Identify packed fields and targets by native allocation, not moved offsets."""
    from bisect import bisect_right

    physical, blocks = [], {}
    for index, read in enumerate(trace['reads']):
        if not read['size']:
            continue
        if read['block'] != 4:
            physical.append((read['physical'], read['physical'] + read['size'], 'read', index))
        # Block1 is rewound by native pop events. Its repeated offsets cannot
        # identify a durable owner without an additional lifetime contract.
        if read['block'] not in (1, 4):
            blocks.setdefault(read['block'], []).append(
                (read['offset'], read['offset'] + read['size'], 'read', index))
    for index, insert in enumerate(trace['inserts']):
        require(insert['block'] == 5, 'Unexpected native insertion block')
        blocks.setdefault(5, []).append((insert['offset'], insert['offset'] + 8, 'insert', index))

    def index_spans(spans):
        spans.sort()
        require(all(left[1] <= right[0] for left, right in zip(spans, spans[1:])),
                'Packed reference ownership has overlapping allocations')
        return spans, [span[0] for span in spans]

    physical = index_spans(physical)
    blocks = {block: index_spans(spans) for block, spans in blocks.items()}

    def owner(index, offset, size):
        spans, starts = index
        position = bisect_right(starts, offset) - 1
        require(position >= 0 and offset + size <= spans[position][1],
                'Packed reference leaves its native allocation')
        start, _, kind, number = spans[position]
        return kind, number, offset - start

    identities, fields = [], set()
    for ref in trace['refs']:
        field = ref['physical']
        require(struct.unpack_from('<Q', data, field)[0] == ref['value'],
                'Packed reference trace differs from serialized field')
        require(ref['block'] not in (1, 4) and ref['block'] in blocks,
                'Packed reference lacks a durable native target owner')
        source = owner(physical, field, 8)
        target = owner(blocks[ref['block']], ref['offset'], 8 if ref['indirect'] else 1)
        identities.append((source, ref['asset'], ref['indirect'], ref['block'], target))
        fields.add(field)
    return sorted(identities), fields


def prove_rebuild(before, after, old_trace, new_trace, edits):
    require(len(old_trace['reads']) == len(new_trace['reads']), 'Typed allocation ordering changed')
    require([(a['root'], a['type'], a['name']) for a in old_trace['assets']] ==
            [(a['root'], a['type'], a['name']) for a in new_trace['assets']], 'Asset ownership changed')
    require(old_trace['events'] == new_trace['events'], 'Native allocation event ordering changed')
    require([(row['block'], row['asset']) for row in old_trace['inserts']] ==
            [(row['block'], row['asset']) for row in new_trace['inserts']], 'Native insertion ownership changed')
    old_refs, fields = packed_reference_owners(before, old_trace)
    new_refs, _ = packed_reference_owners(after, new_trace)
    require(old_refs == new_refs, 'Packed reference field or target ownership changed')
    pointers = {i for field in fields for i in range(field, field + 8)}
    changed = []
    for old, new in zip(old_trace['reads'], new_trace['reads']):
        require((old['caller'], old['block'], old['asset']) == (new['caller'], new['block'], new['asset']),
                'A native allocation changed owners')
        at = old['physical']
        expected = edits.get(at, (before[at:at + old['size']], None))[0]
        actual = after[new['physical']:new['physical'] + new['size']]
        require(len(expected) == len(actual) and
                all(a == b or at + i in pointers for i, (a, b) in enumerate(zip(expected, actual))),
                'Unrelated typed bytes changed during VS relocation')
        if at in edits:
            changed.append(dict(before_physical=at, physical=new['physical'], before_bytes=old['size'],
                                bytes=new['size'], before_sha256=sha(before[at:at + old['size']]), sha256=sha(actual)))
    require(len(changed) == len(edits), 'A requested edit lacks a verified typed read')
    return changed


def stage_archive(label, baseline_path, output, replacements):
    policy = BASELINES[label]
    packed = baseline_path.read_bytes()
    require(sha(packed) == policy['fastfile_sha256'], 'Installed fastfile baseline differs: ' + label)
    original = decode(baseline_path)
    require(sha(original) == policy['raw_sha256'] and baseline_path.read_bytes() == packed,
            'Decoded baseline or installed bytes changed: ' + label)
    directory = output / label
    directory.mkdir()
    archive, before = native_walk(ReplayArchive, original, directory, 'before', policy['roots'])
    users = vertex_users(archive, replacements)
    require(users, 'Selected vertex programs have no native technique users')
    edits, definitions = typed_edits(original, archive, replacements)
    require(set(users.values()) <= {row['name'] for row in definitions}, 'A selected VS user has no edited definition')
    changed, _ = rebuild(original, before, edits)
    del archive
    gc.collect()
    archive, after = native_walk(ReplayArchive, changed, directory, 'relocated', policy['roots'])
    typed_changes = prove_rebuild(original, changed, before, after, edits)
    new_users = vertex_users(archive, replacements)
    require(Counter(new_users.values()) == Counter(users.values()), 'VS user closure changed during relocation')
    records = pipeline_records(archive)
    for owners in records.values():
        require(not any(at in new_users for at in owners) or all(at in new_users for at in owners),
                'An edited and untouched technique share pipeline records')
    untouched_keys = {struct.unpack_from('<Q', changed, at)[0] for at, owners in records.items()
                      if all(owner not in new_users for owner in owners)}
    final, keys = finalize_keys(changed, archive, set(new_users))
    require(all(int(row['new'], 16) not in untouched_keys for row in keys['rows']),
            'A new pipeline identity collides with an untouched pipeline')
    require(all(row['new'] != row['old'] for row in keys['rows']), 'Changed VS retained a prior pipeline identity')
    require({row['technique'] for row in keys['rows']} == set(new_users), 'Affected pipeline coverage is incomplete')
    del archive
    gc.collect()
    raw_path = output / (policy['name'] + '.raw')
    raw_path.write_bytes(final)
    archive, load = native_walk(ReplayArchive, final, directory, 'load', policy['roots'])
    final_users = vertex_users(archive, replacements)
    again, repeated = finalize_keys(final, archive, set(final_users))
    require(again == final and repeated['changed_bytes'] == 0, 'Final PSO identities are not idempotent')
    if label == 'companion':
        require(sorted(a['name'] for a in archive.assets if a['type'] == 18) == sorted(NAMES)
                and len(archive.techniques) == 61, 'Six-set companion scope changed')
        require(all(not a['name'].startswith(',') for a in archive.assets), 'Companion contains an unresolved import')
    technique_count = len(archive.techniques)
    del archive
    gc.collect()
    archive, preload = native_walk(PreloadArchive, final, directory, 'preload', policy['roots'])
    del archive
    gc.collect()
    maximum = [max(a, b) for a, b in zip(load['maximum'], preload['maximum'])]
    result = bytearray(custom_container(packed[:136], final, maximum, preload['preload_walk_size']))
    struct.pack_into('<I', result, 12, 4087)
    fastfile_path = output / (policy['name'] + '.ff')
    fastfile_path.write_bytes(result)
    require(decode(fastfile_path) == final, 'Candidate fastfile did not round-trip')
    require(baseline_path.read_bytes() == packed, 'Installed baseline changed during staging')
    receipt = dict(baseline_path=str(baseline_path), baseline_sha256=sha(packed),
                   fastfile_path=str(fastfile_path), fastfile_size=len(result), fastfile_sha256=sha(result),
                   raw_path=str(raw_path), raw_size=len(final), raw_sha256=sha(final), root_count=policy['roots'],
                   processed_roots=load['processed'], load_complete=True, preload_complete=True,
                   techniques=technique_count, selected_techniques=len(final_users), definitions=definitions,
                   typed_changes=typed_changes, pso_keys=keys, pso_keys_idempotent=True,
                   untouched_pipelines_preserved=True, unrelated_typed_bytes_preserved=True,
                   depth_blend_material_arguments_and_pixel_programs_preserved=True)
    for phase in ('load', 'preload'):
        path = directory / (phase + '.trace.json')
        receipt[phase + '_trace_path'] = str(path)
        receipt[phase + '_trace_sha256'] = sha(path.read_bytes())
    if label == 'companion':
        receipt.update(exact_techsets=list(NAMES), unresolved_imports=0, additional_techsets=0)
    write_json(directory / 'restoration.json', receipt)
    print(label, 'candidate', sha(result), len(result), 'bytes;', len(final_users), 'affected techniques', flush=True)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--contracts', type=Path, required=True)
    parser.add_argument('--bindings', type=Path, required=True)
    parser.add_argument('--bindings-sha256', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    require(output.is_relative_to(PROJECT) and not output.exists(), 'Candidate must be a new project directory')
    require(sha((game / 'game_dx12_ship_replay.exe').read_bytes()) == REPLAY_SHA256, 'Native reader executable differs')
    contract_path, binding_path = args.contracts.resolve(), args.bindings.resolve()
    require(sha(contract_path.read_bytes()) == CONTRACT_SHA256, 'Reviewed native shader contracts differ')
    contracts = read_json(contract_path)
    require(sha(binding_path.read_bytes()) == args.bindings_sha256.lower(), 'Reviewed binding proof differs')
    bindings = read_json(binding_path)
    require(bindings['passed'] and not bindings['failures'] and bindings['source_manifest_sha256'] == CONTRACT_SHA256,
            'All-user native binding proof did not pass')
    require(bindings['map_uses_checked'] == 966 and bindings['companion_uses_checked'] == 61,
            'All-user binding proof has incomplete coverage')
    output.mkdir()
    program_output = output / 'vertex-programs'
    program_output.mkdir()
    replacements, proofs = load_replacements(contracts, program_output)
    write_json(output / 'vertex-programs.json', proofs)
    files = {}
    for label, policy in BASELINES.items():
        source = game / 'magma/maps/mp_sm_island_1' / (policy['name'] + '.ff')
        files[label] = stage_archive(label, source, output, replacements)
    bundle = dict(schema=1, mode='replay120_native_vehicle_vertex_programs',
                  created_utc=datetime.now(timezone.utc).isoformat(), files=files,
                  manifest_path=str(contract_path), manifest_sha256=CONTRACT_SHA256,
                  binding_proof_path=str(binding_path), binding_proof_sha256=sha(binding_path.read_bytes()),
                  programs=proofs, installed_files_modified=False, client_rendering_verified=False,
                  changes=dict(vertex_programs=len(proofs),
                               affected_techniques={key:row['selected_techniques'] for key,row in files.items()},
                               preserved='Pixel programs, material arguments, depth/blend state and unrelated typed data'),
                  validation='Both native LOAD/PRELOAD traversals, exact packed round-trip and typed-byte preservation')
    destination = output / 'vehicle-renderer-r2-bundle.json'
    write_json(destination, bundle)
    print('bundle', destination, sha(destination.read_bytes()), flush=True)


if __name__ == '__main__':
    main()
