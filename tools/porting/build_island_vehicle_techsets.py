"""Stage the six missing Island vehicle technique sets with owned shader closure.

The exact Source definitions are read from complete native archive traces. An
isolated original graph is round-tripped through Source's reader before the
existing Replay layout, format, resource and sampler conversions are applied.
This tool writes a new staging directory only; it does not deploy any asset.
Native traversal is not evidence that the resulting vehicle has rendered.
"""
from __future__ import annotations

import argparse
import bisect
from datetime import datetime, timezone
import gc
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib


PROJECT = Path(__file__).resolve().parents[2]
REPO = PROJECT.parents[1]
EVIDENCE = REPO / 'evidence/island-backport-20261013'
sys.path[:0] = [str(EVIDENCE), str(EVIDENCE/'pipeline120'),
                str(REPO/'tools/fastfiles'), str(REPO/'build/fastfile-python')]

from source164.walk_source164 import Source164
from replay_walk import ReplayArchive, decode, game
from preload120.walk_preload import PreloadArchive
from techset120.convert import rebuild, techset_edits
from pipeline120.remap_samplers import rewrite as rewrite_samplers
from pipeline120.extract_common_shaders import stage as shader_stage
from port_archive import custom_container
from dxbc import chunks, retail_hash


MARKER = 0xFFFFFFFFFFFFFFFE
INLINE = (MARKER, 0xFFFFFFFFFFFFFFFF)
SHADER_TYPES = (14, 15, 16, 17)
STAGES = {14: 'vertex', 15: 'hull', 16: 'domain', 17: 'pixel'}
NAMES = (
    'mo/lit_3_lit_rpl_ta1_200000040804000_1002000000000010_0_1_1_0_0_138100143_0_0_1_0_0',
    'mo/lit_3_lit_rpl_ta1_200000040800840_1002000000000010_0_1_1_0_0_17814415b_0_0_1_0_0',
    'mo/lit_3_lit_rpl_ta1_200000040800840_3002000000000010_0_4_1_0_0_13814415b_0_0_1_0_0_0_0_3810015b_0_1_2_0_0_0_0_3810015b_1_1_2_0_0_0_0_410005b_0_0_2_0_0',
    'mo/lit_3_lit_rpl_ta1_200000040801040_1002000000000010_0_1_1_0_0_13814015b_0_0_1_0_0',
    'mo/lit_3_lit_bad_tca_200000040a80842_1002000000000010_0_1_1_0_0_1f814415b_0_0_3_0_0',
    'mo/lit_3_lit_rpl_ta1_200000040808840_3002000000000010_0_2_1_0_0_1b814415b_0_0_1_0_0_0_0_3810015b_1_1_2_0_0',
)
SOURCE_HASHES = {
    'techsets_common_base_mp': 'b5994a60953d0a8f888a80435726d076ade2a084f3de320e7bf386117ee9de7d',
    'techsets_common_br_mp': 'b37e665e555c3506869ad18668a9f6585a2b09de7b21af05dcee0570cfc11f21',
    'techsets_global_stream_mp': 'e1686c281d74b4bb358d828843cbfcc2c7de4b6445f9c89195fce97fa420015a',
}
REPLAY_SHA256 = '68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0'
RESTORED_RAW_SHA256 = '8e7090336be5f40b4a271b838581aba33aefe39de0f3bcdbc972b9451f423c74'
RESTORED_FF_SHA256 = '5d5bd7b0a9db8c542ca144557624a2afc7c3c39b82b07c7ccc13adf460b9cd70'
RESTORED_RECEIPT_SHA256 = '9198f31652b9c554bcd57a5176dd054647fc13fb2ece7a3e99b03c58db43fb0a'
DEPTH_RAW_SHA256 = 'ca96400512ffb809db61c1ce850eec4e46c6adfe0f8249f8724723507302e970'
DEPTH_FF_SHA256 = '19ac07fbc62eb8f91c06692a182d3898804b3459aedabddf2e4e10307baf36be'
DEPTH_RECEIPT_SHA256 = '17fea8f3f0a95a29e67af057edc9e6b1c0aea9a8fd71bf65154d9657145a6b9c'
SOURCE_PLANS_SHA256 = '571555e47dfd1e34d42d9ea8813585614ac09a56bbf980326b1cdf8c47be48bd'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_json(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


def qword(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]


def point(header, offset, present=True):
    struct.pack_into('<Q', header, offset, MARKER if present else 0)


def text_bytes(value):
    require(bool(value) and '\0' not in value, 'Invalid graph name')
    return value.encode() + b'\0'


def complete(trace, data):
    require(trace['complete'] and not trace.get('error') and
            trace['processed'] == trace['count'] and trace['physical_size'] == len(data),
            'Native traversal did not complete at exact EOF')


class SourceGraph:
    """Read only allocations and fields already consumed by the native reader."""

    def __init__(self, data, trace, label):
        complete(trace, data)
        require(trace.get('input_sha256') == sha(data), 'Source trace payload hash differs')
        self.data, self.trace, self.label = data, trace, label
        self.reads = sorted((r for r in trace['reads'] if r['size'] and r['block'] != 4),
                            key=lambda r: r['physical'])
        self.starts = [r['physical'] for r in self.reads]
        self.refs = {r['physical']: r for r in trace['refs']}
        self.strings = {r['field']: r for r in self.reads if 'field' in r}
        self.assets = {r['physical']: r for r in trace['assets']}
        self.techniques = {r['physical']: r for r in trace['techniques']}
        self.technique_starts = sorted(self.techniques)
        self.root_ends = {}
        for row in trace['assets']:
            self.root_ends[row['root']] = max(self.root_ends.get(row['root'], 0), row['end'])

    def span(self, at, size):
        i = bisect.bisect_right(self.starts, at) - 1
        require(i >= 0 and size >= 0, 'Missing native allocation')
        owner = self.reads[i]
        require(owner['physical'] <= at and at + size <= owner['physical'] + owner['size'],
                'Requested bytes leave a native typed allocation')
        return self.data[at:at + size]

    def string(self, field):
        value = qword(self.data, field)
        if value in INLINE:
            require(field in self.strings, 'Inline string lacks native field ownership')
            read = self.strings[field]
            at, size = read['physical'], read['size']
        else:
            ref = self.refs.get(field)
            require(ref and not ref['indirect'] and ref['target'] is not None,
                    'String lacks a proven direct reference')
            at = ref['target']
            read = self.reads[bisect.bisect_right(self.starts, at) - 1]
            size = read['physical'] + read['size'] - at
        raw = self.span(at, size)
        end = raw.find(b'\0')
        require(0 <= end < 2048, 'Name has no bounded terminator')
        return raw[:end].decode('utf-8')

    def array(self, field, size, caller, first, end):
        value = qword(self.data, field)
        if not value:
            require(size == 0, 'Required native array is absent')
            return b'', None
        if value in INLINE:
            matches = [r for r in self.reads if first <= r['physical'] < end and
                       r['caller'] == caller and r['size'] == size]
            require(len(matches) == 1, f'Inline array owner is ambiguous: {field:#x}/{caller:#x}')
            at = matches[0]['physical']
        else:
            ref = self.refs.get(field)
            require(ref and not ref['indirect'] and ref['target'] is not None,
                    'Array lacks a proven direct reference')
            at = ref['target']
        return self.span(at, size), at

    def shader_name(self, field, kind, first, end):
        value = qword(self.data, field)
        if not value:
            return None
        if value in INLINE or value == MARKER - 1:
            matches = [a for a in self.assets.values() if a['type'] == kind and
                       first <= a['physical'] < end]
            require(len(matches) == 1, 'Inline shader ownership is ambiguous')
            asset = matches[0]
        else:
            ref = self.refs.get(field)
            require(ref and ref['indirect'] and ref['target'] in self.assets,
                    'Shader lacks an exact native registered owner')
            asset = self.assets[ref['target']]
        require(asset['type'] == kind and asset['size'] == 40, 'Shader owner type/size differs')
        return asset['name'].lstrip(',')

    def technique(self, at):
        require(at in self.techniques, 'Technique lacks a native reader entry')
        row = self.techniques[at]
        index = bisect.bisect_right(self.technique_starts, at)
        end = self.root_ends[row['asset']]
        if index < len(self.technique_starts):
            end = min(end, self.technique_starts[index])
        header = self.span(at, 176)
        layouts, formats = header[14], header[123]
        require(layouts and formats and not any(header[40:72]), 'Unsupported technique runtime fields')
        # Source2BAD302..2BAD325 adds the four byte counts at70..73,
        # then multiplies by the six-byte argument stride. The word at78
        # describes a different renderer allocation and is not this size.
        argument_bytes = sum(header[112:116])*6
        arrays = {}
        for key, field, size, caller in (
            ('pipeline', 32, layouts*formats*16, 0x2BAD0F4),
            ('layout', 72, 16 if qword(header,72) else 0, 0x2BAD181),
            ('formats', 128, formats*5, 0x2BAD285),
            ('arguments', 168, argument_bytes, 0x2BAD32A),
        ):
            arrays[key] = self.array(at+field, size, caller, at, end)[0].hex()
        shaders = [self.shader_name(at+field, kind, at, end)
                   for field, kind in zip((80,88,96,104), SHADER_TYPES)]
        return dict(name=self.string(at), type=struct.unpack_from('<I',header,8)[0],
                    header=header.hex(), arrays=arrays, shaders=shaders,
                    source=dict(archive=self.label, physical=at, root=row['asset']))

    def techset(self, asset):
        at, end = asset['physical'], asset['end']
        header = self.span(at, 64)
        require(header.hex() == asset['serialized_header'], 'Registered techset header differs')
        masks = struct.unpack_from('<4Q', header,24)
        slots = [word*64+bit for word,mask in enumerate(masks) for bit in range(64) if mask & (1<<bit)]
        raw, array_at = self.array(at+56, len(slots)*8, 0x2BAD515, at, end)
        own = iter(t['physical'] for t in self.trace['techniques'] if t['asset']==asset['root'])
        rows = []
        for index, slot in enumerate(slots):
            value = qword(raw,index*8)
            if value in INLINE:
                physical = next(own, None)
                require(physical is not None, 'Inline technique is missing')
            else:
                ref = self.refs.get(array_at+index*8)
                require(ref and not ref['indirect'] and ref['target'] in self.techniques,
                        'Technique pointer lacks a native body')
                physical = ref['target']
            row = self.technique(physical)
            require(row['type'] == slot, 'Technique mask/type differs')
            rows.append(row)
        require(next(own,None) is None, 'Unowned inline technique remains')
        return dict(name=asset['name'], header=header.hex(), techniques=rows,
                    source=dict(archive=self.label, root=asset['root'], physical=at, end=end))


def validate_shader(kind, name, code):
    if not code:
        require(name in ('null.hlsl','null_vshsds.hlsl'), 'Unresolved empty shader: '+name)
        return
    require(shader_stage(code) == STAGES[kind], 'Graphics shader stage differs')
    require(retail_hash(code[20:]) == code[4:20], 'Original shader checksum differs')
    list(chunks(code))


def shader_definitions(directory, requests):
    """Use the existing common-shader extraction contract for exact-name programs."""
    found = {}
    for path in sorted(directory.glob('*.raw')):
        raw = path.read_bytes()
        for kind, name in sorted(requests):
            needle, cursor = text_bytes(name), 0
            while True:
                at = raw.find(needle,cursor)
                if at < 0:
                    break
                cursor = at+1
                start = at-40
                if start < 0:
                    continue
                header = raw[start:at]
                marker,debug,runtime,program,size,fingerprint = struct.unpack('<4Q2I',header)
                if (marker not in INLINE or runtime or program not in (0,*INLINE) or
                        not 0 <= size <= 8*1024*1024 or bool(program) != bool(size)):
                    continue
                code_at = at+len(needle)
                if debug in INLINE:
                    terminator = raw.find(b'\0',code_at,code_at+256)
                    require(terminator >= code_at, 'Shader debug name is unbounded')
                    code_at = terminator+1
                code = raw[code_at:code_at+size]
                require(len(code) == size, 'Truncated original shader program')
                validate_shader(kind,name,code)
                key = (kind,name)
                digest = sha(code)
                if key in found:
                    require(found[key]['sha256'] == digest and found[key]['fingerprint'] == fingerprint,
                            'Conflicting exact-name shader definitions: '+name)
                    continue
                found[key] = dict(kind=kind,name=name,code=code,sha256=digest,fingerprint=fingerprint,
                                  source_archive=str(path),source_header=start,source_program=code_at)
        del raw
    require(set(found) == set(requests), 'Missing authentic shader definitions: '+str(sorted(set(requests)-set(found))))
    return found


def serialize_source(plans, shaders):
    ordered = sorted(shaders)
    shader_roots = {key:index for index,key in enumerate(ordered)}
    root_types = [key[0] for key in ordered] + [18]*len(plans)
    bodies, program_spans = [], []
    cursor = 32+len(root_types)*16
    for key in ordered:
        shader = shaders[key]
        code, name = shader['code'],text_bytes(shader['name'])
        header = struct.pack('<4Q2I',MARKER,0,0,MARKER if code else 0,len(code),shader['fingerprint'])
        body = header+name+code
        program_spans.append(dict(key=key,header=cursor,program=cursor+40+len(name),bytes=len(code)))
        bodies.append(body)
        cursor += len(body)
    for plan in plans:
        header = bytearray.fromhex(plan['header'])
        point(header,0)
        point(header,56,bool(plan['techniques']))
        body = bytearray(header+text_bytes(plan['name'])+struct.pack('<Q',MARKER)*len(plan['techniques']))
        for row in plan['techniques']:
            h = bytearray.fromhex(row['header'])
            arrays = {key:bytes.fromhex(value) for key,value in row['arrays'].items()}
            point(h,0)
            for field,key in ((32,'pipeline'),(72,'layout'),(128,'formats'),(168,'arguments')):
                point(h,field,bool(arrays[key]))
            for field,kind,name in zip((80,88,96,104),SHADER_TYPES,row['shaders']):
                value = ((5<<32)+16*shader_roots[(kind,name)]+8+1) if name else 0
                struct.pack_into('<Q',h,field,value)
            body.extend(h)
            body.extend(text_bytes(row['name']))
            for key in ('pipeline','layout','formats','arguments'):
                body.extend(arrays[key])
        bodies.append(bytes(body))
    raw = (struct.pack('<4Q',0,0,len(root_types),MARKER)+
           b''.join(struct.pack('<IIQ',kind,0,MARKER) for kind in root_types)+b''.join(bodies))
    return raw, program_spans


def canonical_plan(plan):
    result = dict(name=plan['name'],header=plan['header'],techniques=[])
    head = bytearray.fromhex(plan['header'])
    for field in (0,56):point(head,field,bool(qword(head,field)))
    result['header'] = head.hex()
    for row in plan['techniques']:
        header = bytearray.fromhex(row['header'])
        for field in (0,32,72,80,88,96,104,128,168):point(header,field,bool(qword(header,field)))
        result['techniques'].append(dict(name=row['name'],type=row['type'],header=header.hex(),
                                         arrays=row['arrays'],shaders=row['shaders']))
    return result


def native_walk(reader, data, output, label, expected_roots):
    archive = reader(data)
    try:
        trace = archive.run()
    except Exception as error:
        archive.error = str(error)
        trace = archive.report()
    trace.update(assets=archive.assets,payload_sha256=sha(data))
    write_json(output/(label+'.trace.json'),trace)
    complete(trace,data)
    require(trace['count'] == expected_roots, 'Native root count differs')
    names = [a['name'] for a in archive.assets if a['type']==18]
    require(sorted(names) == sorted(NAMES), 'Companion registered additional/missing techsets')
    require(all(a['type'] in (*SHADER_TYPES,18) and not a['name'].startswith(',') for a in archive.assets),
            'Companion retained an external or unrelated asset')
    print(label,expected_roots,'roots; exact EOF',len(data),flush=True)
    return archive,trace


def finalize_keys(raw, archive, technique_filter=None):
    """Apply final PSO identities, optionally to an exact set of native owners."""
    complete(archive.report(),raw)
    require(archive.data == raw, 'PSO graph differs from native reader')
    read_index = {r['physical']:r for r in archive.reads}
    shaders = {}
    def shader(pointer):
        if not pointer:return None
        if pointer in shaders:return shaders[pointer]
        header = bytes(archive.u.mem_read(pointer,40))
        size,checksum = struct.unpack_from('<II',header,32)
        program = qword(header,24)
        if not size:
            require(not program, 'Empty shader has a program pointer')
            if technique_filter is not None:
                owner = [a for a in archive.assets if a['physical'] == archive.locate(pointer)]
                require(len(owner) == 1 and owner[0]['name'] in ('null.hlsl', 'null_vshsds.hlsl'),
                        'A selected pipeline contains a named unresolved shader')
            shaders[pointer] = None
            return None
        require(program and size <= 8*1024*1024, 'Unresolved native shader')
        at = archive.locate(program)
        code = bytes(archive.u.mem_read(program,size))
        require(code == raw[at:at+size] and retail_hash(code[20:]) == code[4:20],
                'Final shader is not original/remapped serialized DXBC')
        value = dict(size=size,sha256=sha(code),checksum=checksum)
        shaders[pointer] = value
        return value
    edits,identities,rows,selected = {},{},[],set()
    for technique in archive.techniques:
        at,resident = technique['physical'],technique['resident']
        if technique_filter is not None and at not in technique_filter:
            continue
        selected.add(at)
        header = raw[at:at+184]
        require(read_index[at]['caller']==0xE0CEF1 and len(header)==184,'Replay technique extent differs')
        layouts,formats = header[14],header[131]
        require(layouts and formats,'Missing native pipeline layout/format')
        pso = archive.locate(archive.q(resident+40))
        count = layouts*formats
        require(read_index[pso]['caller']==0xE0CF77 and read_index[pso]['size']==16*count,
                'Pipeline records lack exact native ownership')
        format_bytes = bytes(archive.u.mem_read(archive.q(resident+136),formats*5))
        layout_pointer = archive.q(resident+80)
        layout = bytes(archive.u.mem_read(layout_pointer,16)) if layout_pointer else b''
        require(not layout or (layout[0]<=15 and all(v<15 for v in layout[1:1+layout[0]])),
                'Unmapped input-layout attribute')
        declarations = list(header[15:39])
        require(all(v==255 or v<layouts for v in declarations),'Unmapped declaration layout')
        base = dict(technique_metadata=header[8:40].hex(),argument_metadata=header[128:136].hex(),
                    state_metadata=header[156:176].hex(),declarations=declarations,attributes=layout.hex(),
                    renderer='Replay-1.20.4.7623265-ECB4D0',
                    shaders={name:shader(archive.q(resident+field))
                             for name,field in (('VS',88),('HS',96),('DS',104),('PS',112))})
        for index in range(count):
            physical = pso+index*16
            old,runtime = struct.unpack_from('<QQ',raw,physical)
            require(old and not runtime,'Pipeline is not an uninstantiated cache identity')
            identity = dict(base,layout_slot=index%layouts,
                            format_row=format_bytes[5*(index//layouts):5*(index//layouts)+5].hex())
            canonical = json.dumps(identity,sort_keys=True,separators=(',',':')).encode()
            digest = hashlib.sha256(b'iw8x/island/replay120/final-pso/v1\0'+canonical).digest()
            key = int.from_bytes(digest[:8],'little')
            require(key and (key not in identities or identities[key]==digest.hex()),'Pipeline identity collision')
            require(physical not in edits or edits[physical]==key,'Shared incompatible pipeline identity')
            identities[key],edits[physical] = digest.hex(),key
            rows.append(dict(technique=at,type=struct.unpack_from('<I',header,8)[0],physical=physical,
                             old=hex(old),new=hex(key),identity_sha256=digest.hex()))
    require(technique_filter is None or selected == set(technique_filter),
            'PSO filter contains an unknown native technique')
    result = bytearray(raw)
    for physical,key in edits.items():struct.pack_into('<Q',result,physical,key)
    allowed = {i for physical in edits for i in range(physical,physical+8)}
    require(all(a==b or i in allowed for i,(a,b) in enumerate(zip(raw,result))),
            'PSO finalization modified unrelated archive bytes')
    return bytes(result),dict(pipeline_records=len(edits),unique_keys=len(identities),rows=rows,
                              changed_bytes=sum(a!=b for a,b in zip(raw,result)),only_pipeline_key_bytes_changed=True)


def apply_depth_ownership(raw, archive):
    """Move only the five opaque families' ordinary color depth to the main pass.

    Replay ECB79A..ECB7D9 decodes write bit 0x200 and the reversed-depth
    comparison table [ALWAYS, LESS_EQUAL, EQUAL, GREATER_EQUAL]. These exact
    Source prepass/EQUAL pairs have the same ownership problem investigated in
    render_audit/asset_pso/stage_main_depth_authority.py. Include the two velocity
    prepasses used by dynamic models, preserving their stencil/cull bits. The
    stencil, object-ID, shadow and translucent passes are outside this policy.
    This produces a candidate for a live rendering retest, not visual proof.
    """
    complete(archive.report(),raw)
    require(archive.data == raw,'Depth graph differs from native reader')
    require(len(archive.techniques)==61,'Unexpected six-set technique count')
    owners = {a['root']:a['name'] for a in archive.assets if a['type']==18}
    require(sorted(owners.values())==sorted(NAMES),'Depth policy techset ownership differs')
    profiles = {}
    for index in (0,1,2,3,5):
        cull = 0 if index==5 else 1
        velocity = 0x5400040002002E00 if index==5 else 0x5400040000002E01
        profiles[NAMES[index]] = {
            0:(0xE00|cull,0xFFFFFFF000000000,2),
            3:(velocity,0xFFFFFF3000000000,2),
            6:(velocity,0xFFFFFF3000000000,2),
            34:(0x800|cull,0,1),
        }
    rows,seen = [],set()
    for technique in archive.techniques:
        at = technique['physical']
        name = owners[technique['asset']]
        header = raw[at:at+184]
        kind = struct.unpack_from('<I',header,8)[0]
        expected = profiles.get(name,{}).get(kind)
        if expected is None:
            continue
        require((name,kind) not in seen,'Duplicate opaque depth pass')
        seen.add((name,kind))
        state,blend = struct.unpack_from('<QQ',header,160)
        require((state,blend,header[131])==expected and header[14]==1,
                'Opaque depth state, blend or pipeline extent differs: '+name+'/'+str(kind))
        changed = ((state & ~0xE00) | 0xE00) if kind==34 else (state & ~0x200)
        require(changed!=state,'Depth policy is already applied')
        rows.append(dict(techset=name,type=kind,technique=at,physical=at+160,
                         before=hex(state),after=hex(changed),blend=hex(blend),
                         pipelines=header[14]*header[131]))
    require(seen=={(name,kind) for name,types in profiles.items() for kind in types},
            'Missing exact opaque depth pass')
    require(len(rows)==20 and sum(row['pipelines'] for row in rows)==35,
            'Depth conversion scope differs')
    result = bytearray(raw)
    for row in rows:
        struct.pack_into('<Q',result,row['physical'],int(row['after'],16))
    allowed = {i for row in rows for i in range(row['physical'],row['physical']+8)}
    require(all(a==b or i in allowed for i,(a,b) in enumerate(zip(raw,result))),
            'Depth conversion modified unrelated archive bytes')
    return bytes(result),dict(policy='opaque-main-depth-ownership-v1',rows=rows,
                              changed_techniques=20,affected_pipeline_records=35,
                              changed_bytes=sum(a!=b for a,b in zip(raw,result)),
                              comparison='GREATER_EQUAL',color_write_masks_unchanged=True,
                              dynamic_prepass_types=[0,3,6],main_type=34,
                              translucent_family_unchanged=NAMES[4],live_rendering_verified=False)


def preserve_depth_ownership(raw, archive, baseline):
    """Retain the tested depth A/B while applying only the shader interface fix."""
    require(baseline['changed_techniques']==20 and len(baseline['rows'])==20,
            'Pinned depth policy receipt differs')
    owners = {a['root']:a['name'] for a in archive.assets if a['type']==18}
    techniques = {(owners[t['asset']],struct.unpack_from('<I',raw,t['physical']+8)[0]):t
                  for t in archive.techniques}
    rows = []
    for old in baseline['rows']:
        technique = techniques[(old['techset'],old['type'])]
        at = technique['physical']
        state,blend = struct.unpack_from('<QQ',raw,at+160)
        require(state==int(old['after'],16),'Shader projection changed a reviewed depth word')
        rows.append(dict(old,technique=at,physical=at+160,blend=hex(blend),
                         blend_before_renderer_projection=old['blend']))
    return dict(baseline,rows=rows,states_preserved_from_sha256=DEPTH_RAW_SHA256,
                depth_states_reapplied=False)


def stage_replay_candidate(raw, output, root_count, template, provenance, pinned_baseline=False,
                           preserved_depth=None):
    original = raw
    archive,before = native_walk(ReplayArchive,raw,output,'replay-before-depth',root_count)
    if pinned_baseline:
        unchanged,keys = finalize_keys(raw,archive)
        require(unchanged==raw and keys['changed_bytes']==0 and keys['pipeline_records']==91,
                'Reviewed baseline PSO identities differ')
    if preserved_depth is None:
        raw,depth = apply_depth_ownership(raw,archive)
    else:
        require(not pinned_baseline,'Renderer and original depth staging are distinct inputs')
        depth = preserve_depth_ownership(raw,archive,preserved_depth)
    del archive,before
    gc.collect()
    archive,before = native_walk(ReplayArchive,raw,output,'replay-before-keys',root_count)
    raw,key_receipt = finalize_keys(raw,archive)
    del archive,before
    gc.collect()
    require(key_receipt['pipeline_records']==91,'Final pipeline coverage differs')
    if pinned_baseline:
        changed_keys = [r for r in key_receipt['rows'] if r['old']!=r['new']]
        affected = {r['technique'] for r in depth['rows']}
        require(len(changed_keys)==35 and
                all((r['old']!=r['new'])==(r['technique'] in affected) for r in key_receipt['rows']),
                'Depth policy did not rekey exactly its owned pipelines')
        allowed = {i for row in depth['rows'] for i in range(row['physical'],row['physical']+8)}
        allowed.update(i for row in changed_keys for i in range(row['physical'],row['physical']+8))
        require(len(raw)==len(original) and
                all(a==b or i in allowed for i,(a,b) in enumerate(zip(original,raw))),
                'Candidate changed bytes outside its exact state and PSO key fields')
        depth.update(baseline_sha256=sha(original),only_state_and_owned_key_bytes_changed=True,
                     changed_pipeline_keys=35,total_changed_bytes=sum(a!=b for a,b in zip(original,raw)))
    (output/'techsets_magma_island_common_stream.raw').write_bytes(raw)
    archive,load = native_walk(ReplayArchive,raw,output,'load',root_count)
    twice,repeat = finalize_keys(raw,archive)
    require(twice==raw and repeat['changed_bytes']==0,'Final PSO identities are not idempotent')
    del archive
    gc.collect()
    archive,preload = native_walk(PreloadArchive,raw,output,'preload',root_count)
    del archive
    maxima = [max(a,b) for a,b in zip(load['maximum'],preload['maximum'])]
    packed = bytearray(custom_container(template[:136],raw,maxima,preload['preload_walk_size']))
    struct.pack_into('<I',packed,12,4087)
    destination = output/'techsets_magma_island_common_stream.ff'
    destination.write_bytes(packed)
    require(decode(destination)==raw,'Packed companion did not round-trip')
    receipt = dict(provenance)
    receipt.update(created_utc=datetime.now(timezone.utc).isoformat(),
                   output=str(destination),sha256=sha(packed),bytes=len(packed),
                   decoded_sha256=sha(raw),decoded_bytes=len(raw),roots=root_count,
                   exact_techsets=list(NAMES),techniques=61,unresolved_imports=0,additional_techsets=0,
                   native_load_complete=True,native_preload_complete=True,
                   pso_keys_idempotent=True,pso_keys=key_receipt,depth_ownership=depth,
                   client_rendering_verified=False,
                   remaining_runtime_validation='Vehicle body rendering, material appearance and live shader resource behavior',
                   template_sha256=sha(template),installed_files_modified=False)
    write_json(output/'techset-restoration.json',receipt)
    print(json.dumps({k:receipt[k] for k in ('output','sha256','bytes','decoded_sha256','decoded_bytes','roots',
                                          'techniques','native_load_complete','native_preload_complete',
                                          'client_rendering_verified')},indent=2),flush=True)


def stage_reviewed_baseline(directory, output):
    receipt_path = directory/'techset-restoration.json'
    raw_path = directory/'techsets_magma_island_common_stream.raw'
    ff_path = directory/'techsets_magma_island_common_stream.ff'
    raw,template = raw_path.read_bytes(),ff_path.read_bytes()
    require(sha(raw)==RESTORED_RAW_SHA256 and sha(template)==RESTORED_FF_SHA256 and
            sha(receipt_path.read_bytes())==RESTORED_RECEIPT_SHA256,
            'Reviewed vehicle restoration baseline differs')
    require(len(raw)==344188 and len(template)==344408 and decode(ff_path)==raw,
            'Reviewed baseline fastfile does not round-trip')
    provenance = read_json(receipt_path)
    require(provenance['source_graph_roundtrip'] and provenance['native_load_complete'] and
            provenance['native_preload_complete'] and provenance['roots']==31 and
            provenance['techniques']==61,'Baseline validation receipt differs')
    output.mkdir(parents=True)
    (output/'source-plans.json').write_bytes((directory/'source-plans.json').read_bytes())
    provenance.update(baseline_receipt=str(receipt_path),baseline_receipt_sha256=sha(receipt_path.read_bytes()),
                      source_graph_roundtrip_repeated=False,
                      source_graph_provenance='Reused exact hash-pinned original restoration; all Replay phases rerun')
    stage_replay_candidate(raw,output,31,template,provenance,pinned_baseline=True)


def stage_renderer_baseline(directory, output):
    """Apply the complete, exact-program renderer projection to the depth A/B."""
    from island_vehicle_shader_projection import project_archive
    receipt_path = directory/'techset-restoration.json'
    raw_path = directory/'techsets_magma_island_common_stream.raw'
    ff_path = directory/'techsets_magma_island_common_stream.ff'
    plan_path = directory/'source-plans.json'
    raw,template = raw_path.read_bytes(),ff_path.read_bytes()
    require(sha(raw)==DEPTH_RAW_SHA256 and sha(template)==DEPTH_FF_SHA256 and
            sha(receipt_path.read_bytes())==DEPTH_RECEIPT_SHA256 and
            sha(plan_path.read_bytes())==SOURCE_PLANS_SHA256,
            'Reviewed shader-projection baseline differs')
    require(len(raw)==344188 and len(template)==344408 and decode(ff_path)==raw,
            'Depth baseline fastfile does not round-trip')
    provenance = read_json(receipt_path)
    require(provenance['native_load_complete'] and provenance['native_preload_complete'] and
            provenance['pso_keys_idempotent'] and provenance['roots']==31 and provenance['techniques']==61,
            'Depth baseline native validation receipt differs')
    output.mkdir(parents=True)
    (output/'source-plans.json').write_bytes(plan_path.read_bytes())
    archive,before = native_walk(ReplayArchive,raw,output,'renderer-input',31)
    unchanged,keys = finalize_keys(raw,archive)
    require(unchanged==raw and keys['changed_bytes']==0 and keys['pipeline_records']==91,
            'Depth baseline PSO identities differ')
    projected,renderer = project_archive(raw,archive,read_json(plan_path)['plans'],
                                        EVIDENCE/'render_audit',output,native_walk)
    del archive,before
    gc.collect()
    changed = {row['name']:row for row in renderer['programs']}
    provenance['shader_definitions'] = [dict(row,sampler_sha256=row['converted_sha256'],
        converted_sha256=changed[row['name']]['sha256'],
        converted_bytes=changed[row['name']].get('bytes',0)) for row in provenance['shader_definitions']]
    provenance.update(baseline_receipt=str(receipt_path),baseline_receipt_sha256=sha(receipt_path.read_bytes()),
                      source_graph_roundtrip_repeated=False,renderer_projection=renderer,
                      source_graph_provenance='Exact hash-pinned Source ownership and depth baseline; Replay phases rerun')
    stage_replay_candidate(projected,output,31,template,provenance,
                           preserved_depth=provenance['depth_ownership'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=Path(r'D:\Games\iw8x-port-stage\island-120-20261013\analysis\common164'))
    parser.add_argument('--trace-dir',type=Path)
    parser.add_argument('--baseline-dir',type=Path,
                        help='Reuse the exact reviewed restoration without re-extracting unchanged Source shaders')
    parser.add_argument('--renderer-baseline-dir',type=Path,
                        help='Project the exact reviewed vehicle depth candidate onto the native renderer ABI')
    parser.add_argument('--template',type=Path,
                        help='Reviewed empty or original restored companion used only for its fastfile header')
    parser.add_argument('--output-dir',type=Path,required=True)
    args = parser.parse_args()
    require(not args.output_dir.exists(),'Use a new staging directory')
    with (game/'game_dx12_ship_replay.exe').open('rb') as stream:
        require(hashlib.file_digest(stream,'sha256').hexdigest()==REPLAY_SHA256,'Replay executable differs')
    if args.renderer_baseline_dir:
        require(args.baseline_dir is None and args.trace_dir is None and args.template is None,
                'Renderer staging takes its own pinned baseline and template')
        stage_renderer_baseline(args.renderer_baseline_dir,args.output_dir)
        return
    if args.baseline_dir:
        require(args.trace_dir is None and args.template is None,'Baseline staging takes its own pinned provenance and template')
        stage_reviewed_baseline(args.baseline_dir,args.output_dir)
        return
    require(args.trace_dir is not None,'Source extraction requires --trace-dir')
    plans,source_receipts = {},[]
    for label,expected_sha in SOURCE_HASHES.items():
        data = (args.source_dir/(label+'.raw')).read_bytes()
        require(sha(data)==expected_sha,'Authenticated Source payload differs: '+label)
        trace_path = args.trace_dir/label/'load-trace.json'
        trace = read_json(trace_path)
        graph = SourceGraph(data,trace,label)
        for asset in trace['assets']:
            if asset['type']==18 and asset['name'] in NAMES:
                require(asset['name'] not in plans,'Duplicate exact Source techset definition')
                plans[asset['name']] = graph.techset(asset)
        source_receipts.append(dict(name=label,sha256=expected_sha,trace=str(trace_path),
                                    trace_sha256=sha(trace_path.read_bytes()),roots=trace['count'],complete=True,
                                    scope='Authenticated decoded Source base fastfile; no additional patch chain claimed'))
        del graph,trace,data
        gc.collect()
    require(set(plans)==set(NAMES),'Missing exact Source techset owner')
    ordered_plans = [plans[name] for name in NAMES]
    requests = {(kind,name) for plan in ordered_plans for row in plan['techniques']
                for kind,name in zip(SHADER_TYPES,row['shaders']) if name}
    print('Exact techsets',len(plans),'techniques',sum(len(p['techniques']) for p in ordered_plans),
          'shader definitions required',len(requests),flush=True)
    shaders = shader_definitions(args.source_dir,requests)
    original,program_spans = serialize_source(ordered_plans,shaders)
    args.output_dir.mkdir(parents=True)
    write_json(args.output_dir/'source-plans.json',dict(sources=source_receipts,plans=ordered_plans))
    (args.output_dir/'original-source.raw').write_bytes(original)
    source = Source164(original,args.output_dir/'source-roundtrip')
    source_trace = source.run()
    complete(source_trace,original)
    write_json(args.output_dir/'source-roundtrip.trace.json',source_trace)
    graph = SourceGraph(original,source_trace,'isolated-source')
    reread = {a['name']:graph.techset(a) for a in source_trace['assets'] if a['type']==18}
    require({n:canonical_plan(p) for n,p in plans.items()} ==
            {n:canonical_plan(p) for n,p in reread.items()},'Source graph round-trip changed native semantics')
    edits = techset_edits(original,source_trace)
    programs = []
    for span in program_spans:
        shader = shaders[tuple(span['key'])]
        code = shader['code']
        changed,operands = rewrite_samplers(code) if code else (code,[])
        require(len(changed)==len(code),'Shader conversion changed original program extent')
        if operands:
            header = bytearray(original[span['header']:span['header']+40])
            struct.pack_into('<I',header,36,zlib.crc32(changed))
            edits[span['header']] = bytes(header),None
            edits[span['program']] = changed,None
        programs.append({k:v for k,v in shader.items() if k!='code'} |
                        dict(converted_sha256=sha(changed),sampler_operands=operands,bytes=len(code)))
    candidate,allocations = rebuild(original,source_trace,edits)
    del graph,source,source_trace,original,shaders
    gc.collect()
    root_count = len(requests)+len(NAMES)
    template_path = args.template or game/'magma/maps/mp_sm_island_1/techsets_magma_island_common_stream.ff'
    template = template_path.read_bytes()
    placeholder = decode(template_path)
    require((len(placeholder)==32 and struct.unpack_from('<Q',placeholder,16)[0]==0) or
            (sha(template)==RESTORED_FF_SHA256 and sha(placeholder)==RESTORED_RAW_SHA256),
            'Template is neither the reviewed empty nor restored companion')
    provenance = dict(sources=source_receipts,shader_definitions=programs,
                   source_graph_roundtrip=True,source_graph_roundtrip_repeated=True,
                   input_declaration_mapping=True,pixel_format_mapping=True,shader_stage_mapping=True,
                   writable_and_sampled_texture_mapping=True,sampler_mapping=True)
    stage_replay_candidate(candidate,args.output_dir,root_count,template,provenance)


if __name__=='__main__':
    main()
