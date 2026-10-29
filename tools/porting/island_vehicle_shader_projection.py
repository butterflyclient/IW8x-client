"""Project only the authenticated Island vehicle shaders onto Replay's GPU ABI.

The map renderer audit proves the native scene, entity and unified model-fetch
layouts. Eighteen programs match that audit exactly. Six additional pixel
programs consume only its already-proven scene/entity fields. Unknown programs,
dynamic scene indices, unmatched loads and reflection data are rejected.

The prepass correction is coupled: output registers/signatures, attachment rows
and inverted color-write masks change together. Depth/stencil/cull words and
material-local buffers are never changed here. This is an offline asset tool.
"""
from __future__ import annotations

from collections import Counter
import gc
import hashlib
import importlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib


# Each tuple is (asset type, post-sampler input SHA256, projected output SHA256).
# These are the complete 25 owned definitions, including the native empty PS.
PROGRAMS = {
    '214b211934540e59d3643a3a04b43d84': (14, '2e409f2d255daddee4f5b7b99f8b038ea1684240c90c5774618b68014400754e', '4e2ac06645c3f97d86599be74b428fd6706cad296c3753ecf750140619b9dc5a'),
    '4291e924ba893944bded34f6e47cae5b': (14, 'fb6ac97ce28d4601108852dfeb2c434f4ddbbb3605d373967e81fd11617d8784', '2e9538058094f2753d9d7d3e1ed1c28363b0f965d221998a2c889984ec58746e'),
    '52c79f423a32f389f1fad8559e75242f': (14, 'bdfd35bb25ece5f81ae7a58211c2341fb82a59f50cf55a75651cd131349b7a32', 'dda554f0e1ed89399c0255a6b9fbc575727b2b01e8f8e5f92a9967598fb395a9'),
    '5ba205b352613b164b16f2983449ee55': (14, '566bfe3af18f147e2712a4a9f901e8ced8f73ce8c65da539120ba4c1a95dbbf7', 'e589c6892c37ff70bac1644613d6db3b8dcdb5b8bc529c481b3944da1cc1ed82'),
    '7259e81bf5fee9ebdd66c58c75ef6144': (14, 'b917282b2968a72e5516f78cd96d7757659aa5e30ebe997fe728bda3625f6665', 'ef3103d53df4c860eef30ff904e77e3c69a5a5b4f8bad1ab8761adcb30f30693'),
    '8038b5dedb2d4d6be5444406bcb780f9': (14, '988acc904d791a8057e636f203157eaa519b696a0ade73abdab90a58da6f07fc', '8b3277b0c41886ff0157d192a11d9fb67fec175a3aadb4bad0dc9f46acfb2808'),
    '86247b3b286c61e760c94cbe3c728f7b': (14, '85c17348268c96ecc93328c34fe3410aec15ab165fc322abfde2a1c52759557d', '0dba5a46107f5fccda9759ac3d2f7cdbd2b5f7ef6f63fcbea4b17c6a534f4022'),
    'aacac813b5cb415b6c2e1dc1677567b8': (14, '7d913bc1da93d624eb780636abec27ab1be816080875fe32cbee89e140e775d9', 'f674453c2fb5372e442dfb7a1b14b4bd39584050072d14c98abf55d117a671c6'),
    'c472cd395fe5c0412ecfc0d0fecb5499': (14, '2ed139c0ca16481178d5224468156f23f3151eb09f6c41596cc557b268bfa102', '131856d1fac10beba8709056c7adcba3b54603a2844baa39b20a579d33e1c9d6'),
    'e74e25a4ec6bb10df1fad8559e75242f': (14, '99278779ec6781035fa77c74f7a7bdcf70abe0e072116eb62e5286d9f298a60d', '61a1c2dd52b5eaf8847b4a37bdceef04a957f2d33765bed27d165eaf6018c9ca'),
    '041fbc55c2d04ee674963edb452d7260': (17, 'ebeb217ff8324b31dfeb301896ae57531795823e9bda4d72d3ad706779970070', '02743b0a83f2af989084d615a5bddc0ecdfd001cfd74981a207b31114a6dcf9b'),
    '10bcfde531d592e741cab44eb5bc0613': (17, '815c910174e6d9178e8b022c291842618a80183b3639a9e1c2bfd3f202f3354e', '0aa141addbacb02bb6560e1b6083e757ecf2577386c9f5160fe10b5e8f2ed77d'),
    '3edcb548aac67f4a00678d86ebadd79f': (17, 'b130d85ae287782bd3791a28b6008d8dbf79dd8a764c7135f6758d9411bf5fa4', 'edd468159f906804a59f7b8f323e805fca644aae0192df60b17a4d1869e0257a'),
    '403283c283c6b4b7486ca810680304fa': (17, 'e7a64186c5b78904653e6f462973b8e0d38f36047a594360d39882f847145269', 'c7dad16aa0e5eb63a291cff1faec7229def387e2524608e3bdb5ba8f224c2d34'),
    '4c7b8d0ac2005035ee231c431edb1b1d': (17, '4b81ceaeabf846c25219ec163c12027e115ffba434d80caafabd04fe34b33850', '24d548429ee5d4f77c9860a7cf1f2a51ef55bea023b4ff51809fbab51a19d964'),
    '6062eda30f55ffff4a604db1bac583f2': (17, '113eaa96a49811fb01105e491d970a0a69f391491ca3b505f3ce2223a6139fa0', 'd7a1ac70ff1d00d1cf8db6a2fe1a0618067888193dabe044e4940379466a6126'),
    '834be730f5b919f4442cfa57a70cabbd': (17, '32b5aeea0700a40595696ae55801b85331e7ac6ef9a598d8c97d1dae5d2e5328', '2ab6d445a73eeb688d669f92fd53084494db1c50d2f742f8214bfa7c597594e9'),
    '84bce0670af11fc19984fdd21b03c6ff': (17, '3d8278f9b86339c1b1a6b4fc7a2c9195478989468bdd65be3f5b114a25de5474', 'aacf345ddc5c3f7949931d4b89d1f676aad658b6bcc4c79869ed58492df08fce'),
    '8ac9c3e150d149b4a261666122019954': (17, '59efe0fce150f011d5b3bcb859adfc939db4e25ac4af8e0e955b07b8651eb8ed', '6d2c24609dfb9be1ca1cfa65808b2a2330ce8406625e371f06b26d942644a115'),
    '9e7555fda8ebbfe7fb4bcea45ede1107': (17, 'd32d70fe7023c563fb888702cd5105251f0b825179ec5c68563c5695caef6ce0', '728392d7df783bd1dbfddd189a5a91e87ad0e5c1c188141b3c2948bdab0e95cb'),
    'ab059296035fcd920f89190c1e7549ef': (17, 'db02e49b85bc4974dfca93be6cc8fb4b17784df2dd9cd6dac74169695a9f82ef', '6a99f5121f7ef39596324d8fcb00cc8b0c02df73e4fe5872cdc600fadd42ce52'),
    'af2659ccc29457683837d686bc0eb85c': (17, 'ff6e89f355609f913beb872b6386b43427bf7be0cff82a531904d179917b9aee', '36510c84b960eb5297385180160d1d2ef73eb1674351679885986cd3156e1a61'),
    'eb548da4b9b5068beaac716474317786': (17, '36de4015894da726219c676813bc7dfe87d143b4a9b71ef64f15e798f98cce9f', '70304995c60c357ae775687bbdc4a7590a1540e17d2de45d5c67199b41649d91'),
    'f6034bd655c6d06bb155aacfc81244b2': (17, 'dc0ff0da1bbad1d3261d9662ef897089e31bb342426544d87b398f73bb2bf11a', '5a295f26694ee31f5f8dec769f88c57e4bf5c5b3d274ce4f372426f118bb17e4'),
    'null.hlsl': (17, 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'),
}
EVIDENCE_HASHES = {
    'program-contracts.json': '03151c7aad17a204f0c04890df0169234e658901520b89e7d5bc5a2d935d3902',
    'model-fetch-addresses.json': '99d9ebcb105de041cfc05e686eb7ba6efd980eb0c825b5dd3ee3edb4d3ed7db1',
    'vertex-fetch-families.json': 'af7bf0e974b5dca4d81b3fbb681cc85a18c02ee9458dd87ab781d95ae2a0f2b0',
    'entity-read-projection.json': '4b265c4c1f6668e2f3f1ca2d336a5d89f1986547ae02ea62e07c4dd983c719af',
    'scene-projection-plan.json': '901deaa8f663ee509f21a7a0b4804cc70b379ef5db898afe96a24387afd40d3f',
    'rewrite_renderer.py': 'abd0b77c7d72075d9aadb6c6b2add25be092b3468f80e7e2787d11c9bff29dca',
    'prepass_outputs.py': '3b03ad8108008705c7263350e180771d67f5bb39dd29162c94217847fa04eac9',
    'audit_programs.py': 'f0eb364dbcba9b080254c56b4f23a8997a8791f9117f32857992cde2b8b14e6e',
    'audit_model_fetch_addresses.py': 'c15f613949e2f7dfc7368f88f0cc8536cab68f8960ba2b1780d63398fc40cc81',
    'audit_vertex_fetch_families.py': '2a2dfd4cf008fda07f77e75b74359da981c703195ea823f97427023474148ce6',
    'entity-field-proof.json': '70421bf3d099be66009e66c58c5152bb951d55396f968411b1822ef85ebf7627',
    'scene-selected-fields-proof.json': 'fee66bfc17bbeb52b13a0814e3949c5354627fc66e7ebb6b134359faf6d0ca4a',
    'scene-upload-proof.json': 'd220348f335876af0dd674af1cfd44a339d29d54e2a308163a3cd353cb539560',
    'emitted-metadata-proof.json': '62ba06e2c510ff31fde648949b212446b6dee5723449016cbd9169bfb435eb74',
    'vertex-upload-layout-proof.json': '0cf0b2460969999559b9a6a41eda67c9fb2c5a21c2488fe12dff9d683955125a',
    'prepass-pixel-proof.json': 'e8a4c6a91b229f19926f3e6cf9ff6fe21dd552c2cfd19efe990a6a1a77cd3408',
    'prepass-output-inventory.json': '6e7bda87549bc6108ecd24567910163c9c75e41e16fc23079f30aeefc92ee74b',
}
COLOR_PREPASS_TYPES = (0, 3, 6, 18, 21, 24)
LANES = 'xyzw'


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


class RendererProjection:
    def __init__(self, evidence, output):
        self.evidence = evidence
        self.output = output
        for name, expected in EVIDENCE_HASHES.items():
            require(sha((evidence/name).read_bytes()) == expected,
                    'Renderer evidence or implementation changed: '+name)
        sys.path.insert(0, str(evidence))
        modules = {}
        for name in ('rewrite_renderer', 'prepass_outputs', 'audit_programs',
                     'audit_model_fetch_addresses', 'audit_vertex_fetch_families'):
            modules[name] = importlib.import_module(name)
            require(Path(modules[name].__file__).resolve() == (evidence/(name+'.py')).resolve(),
                    'Renderer module was imported from a different directory')
        self.renderer = modules['rewrite_renderer']
        self.prepass = modules['prepass_outputs']
        self.interface = modules['audit_programs'].interface
        self.metadata = modules['audit_model_fetch_addresses'].analyze
        self.vertices = modules['audit_vertex_fetch_families'].analyze
        self.operands = modules['audit_model_fetch_addresses'].operands
        from disassemble_shaders import disassemble
        from dxbc import chunks, instructions, retail_hash
        from pipeline120.extract_common_shaders import stage
        self.disassemble, self.chunks, self.retail_hash, self.stage = disassemble, chunks, retail_hash, stage
        self.instructions = instructions
        contracts = read_json(evidence/'program-contracts.json')['programs']
        self.contracts = {row['sha256']:row for row in contracts if row.get('sha256')}
        self.plans = {
            kind:{row['name']:row['loads'] for row in read_json(evidence/name)['rows']}
            for kind, name in (('metadata', 'model-fetch-addresses.json'),
                               ('vertices', 'vertex-fetch-families.json'),
                               ('entities', 'entity-read-projection.json'))
        }
        self.scene = {row['source_index']:row for row in
                      read_json(evidence/'scene-projection-plan.json')['rows']}
        self.entity_fields = {}
        for loads in self.plans['entities'].values():
            for load in loads:
                for field in load['fields']:
                    offset = field['source_offset']
                    mapping = {k:v for k,v in field.items() if k != 'destination_lane'}
                    require(offset not in self.entity_fields or self.entity_fields[offset] == mapping,
                            'Conflicting native entity field proofs')
                    self.entity_fields[offset] = mapping
        self.output.mkdir()
        self.rows = []

    def entity_loads(self, assembly, name):
        result = []
        for number, line in enumerate(assembly.splitlines(), 1):
            if not re.search(r'\bt48\.[xyzw]+', line):
                continue
            args = self.operands(re.sub(r'^.*?\(mixed,mixed,mixed,mixed\)\s*', '', line.strip()))
            require(len(args) == 4 and re.fullmatch(r'l\(\d+\)', args[2]),
                    'Unclassified entity address: '+name)
            start = int(args[2][2:-1])
            mask, swizzle = args[0].rsplit('.', 1)[1], args[3].rsplit('.', 1)[1]
            fields = []
            for lane in mask:
                offset = start+4*LANES.index(swizzle[LANES.index(lane)])
                require(offset in self.entity_fields, 'Entity field lacks an existing native proof')
                fields.append(dict(self.entity_fields[offset], destination_lane=lane))
            result.append(dict(line=number, instruction=line.strip(), fields=fields))
        return result

    def actual_load_counts(self, code):
        counts = Counter()
        for tag, body in self.chunks(code):
            if tag not in (b'SHDR', b'SHEX'):
                continue
            words = list(struct.unpack('<'+'I'*(len(body)//4), body))
            for instruction in self.instructions(words):
                opcode = instruction[0]&0x7FF
                if opcode not in (165, 167):
                    continue
                prefix, args = self.renderer.split(instruction)
                require(len(args) == (3 if opcode == 165 else 4), 'Raw/structured operand extent differs')
                resource = args[-1]
                index = self.renderer.static_index(resource, 7, 1)
                register = resource[index]
                if opcode == 165 and register in (63, 64):
                    counts['metadata' if register == 63 else 'vertices'] += 1
                elif opcode == 167 and register == 48:
                    counts['entities'] += 1
        return counts

    def program(self, code, kind, name, owners):
        require(name in PROGRAMS and (kind, sha(code)) == PROGRAMS[name][:2],
                'Vehicle shader type or complete input fingerprint differs: '+name)
        if not code:
            require(name == 'null.hlsl' and kind == 17, 'Unrecognized empty shader')
            self.rows.append(dict(name=name, empty=True, changed=False, sha256=sha(code)))
            return code
        stage = 'vertex' if kind == 14 else 'pixel'
        require(kind in (14, 17) and self.stage(code) == stage, 'Unexpected shader stage')
        require(self.retail_hash(code[20:]) == code[4:20], 'Input DXBC checksum differs')
        assembly = self.disassemble(code)
        path = self.output/(name+'.before.asm.txt')
        path.write_text(assembly, encoding='ascii')
        program = dict(name=name, sha256=sha(code), stage=stage, **self.interface(assembly))
        exact = self.contracts.get(program['sha256'])
        metadata = self.metadata(path) if 63 in program['raw_buffers'] else []
        vertices = self.vertices(path) if 64 in program['raw_buffers'] else []
        require(all(row['classification'] not in ('unresolved', 'unrecognized_affine') for row in metadata),
                'Unclassified model metadata load')
        require(all(row['family'] in ('position', 'self_visibility', 'tangent_frame', 'uv') for row in vertices),
                'Unclassified model vertex load')
        if metadata or vertices:
            require(exact is not None, 'New model program needs an independent native contract')
            for key, loads in (('metadata', metadata), ('vertices', vertices)):
                require(json.loads(json.dumps(loads)) == self.plans[key].get(exact['name'], []),
                        'Exact model program differs from its audited operand plan')
        entities = self.entity_loads(assembly, name)
        actual_loads = self.actual_load_counts(code)
        require(all(actual_loads[key] == len(loads) for key, loads in
                    (('metadata', metadata), ('vertices', vertices), ('entities', entities))),
                'Native resource load inventory differs from its exact conversion plan')
        if exact:
            require(entities == self.plans['entities'].get(exact['name'], []),
                    'Exact entity program differs from its audited operand plan')
        require(not (set(program['scene_indices'])-set(self.scene)),
                'Used scene operand lacks a native mapping or explicit existing policy')
        # The complete input hash and the plans above are checked before the
        # existing token parser validates opcode, stage, index and load count.
        changed, renderer_edits = self.renderer.rewrite(
            code, program, metadata, vertices, entities, self.scene)
        prepass_owners = [row for row in owners if row['technique'].startswith('TECHNIQUE_DEPTH_PREPASS')]
        declarations = [line for line in assembly.splitlines() if line.startswith('dcl_output')]
        prepass_edits = None
        if kind == 17 and prepass_owners and declarations:
            require(len(prepass_owners) == len(owners) and
                    all(row['type'] in COLOR_PREPASS_TYPES for row in owners),
                    'Prepass pixel shader has an incompatible owner')
            changed, prepass_edits = self.prepass.rewrite_pixel(changed)
        allowed_chunks = (b'SHDR', b'SHEX', b'OSGN') if prepass_edits else (b'SHDR', b'SHEX')
        require([(k,v) for k,v in self.chunks(code) if k not in allowed_chunks] ==
                [(k,v) for k,v in self.chunks(changed) if k not in allowed_chunks],
                'Renderer projection changed an unrelated shader chunk')
        require(sha(changed) == PROGRAMS[name][2], 'Projected program differs from the audited result')
        require(self.retail_hash(changed[20:]) == changed[4:20], 'Projected DXBC checksum differs')
        final_assembly = self.disassemble(changed)
        require('t48, 268' not in final_assembly and 'stride=268' not in final_assembly,
                'Source entity stride remains')
        require(not re.search(r'\bcb7\[(22[1-9]|2[3-9]\d|[3-9]\d\d)\]', final_assembly),
                'Out-of-range Source scene operand remains')
        if prepass_edits:
            require('dcl_output o1.x\n' in final_assembly and 'o1.y' not in final_assembly,
                    'Prepass normal output does not fit the Replay attachment')
        (self.output/(name+'.dxbc')).write_bytes(changed)
        (self.output/(name+'.after.asm.txt')).write_text(final_assembly, encoding='ascii')
        row = dict(name=name, stage=stage, before_sha256=sha(code), sha256=sha(changed),
                   before_bytes=len(code), bytes=len(changed), changed=code != changed,
                   exact_map_program=exact['name'] if exact else None,
                   renderer_edits=renderer_edits, prepass_edits=prepass_edits,
                   actual_load_counts=dict(actual_loads),
                   metadata=metadata, vertices=vertices, entities=entities,
                   scene_plan=[self.scene[index] for index in program['scene_indices']],
                   owners=owners, unrelated_dxbc_chunks_preserved=True)
        self.rows.append(row)
        return changed


def project_archive(raw, archive, source_plans, evidence, output, native_walk):
    """Replace complete typed reads, then verify the native reader's new graph."""
    from techset120.convert import rebuild
    from replay_walk import ReplayArchive
    require(archive.data == raw and archive.report()['complete'], 'Renderer input was not fully traversed')
    require(len(archive.assets) == 31 and len(archive.techniques) == 61, 'Vehicle graph extent differs')
    plan_by_type, shader_owners = {}, {}
    for plan in source_plans:
        for technique in plan['techniques']:
            key = (plan['name'], technique['type'])
            require(key not in plan_by_type, 'Duplicate source technique')
            plan_by_type[key] = technique
            for stage, name in zip(('vertex', 'hull', 'domain', 'pixel'), technique['shaders']):
                if name:
                    shader_owners.setdefault(name, []).append(
                        dict(techset=plan['name'], type=technique['type'],
                             technique=technique['name'], stage=stage))
    projection = RendererProjection(evidence, output/'renderer-programs')
    reads = {row['physical']:row for row in archive.reads}
    edits, changed_types = {}, {}
    shaders = [asset for asset in archive.assets if asset['type'] in (14, 15, 16, 17)]
    require({asset['name'] for asset in shaders} == set(PROGRAMS) and len(shaders) == 25,
            'Restored shader ownership differs')
    for asset in shaders:
        at, kind, name = asset['physical'], asset['type'], asset['name']
        header = raw[at:at+40]
        require(asset['size'] == 40 and reads[at]['size'] == 40, 'Shader header extent differs')
        size = struct.unpack_from('<I', header, 32)[0]
        candidates = [read for read in archive.reads if read['asset'] == asset['root'] and
                      read['size'] == size and at+40 <= read['physical'] < asset['end'] and
                      raw[read['physical']:read['physical']+4] == b'DXBC'] if size else []
        require(len(candidates) == bool(size), 'Program lacks an exact native allocation')
        physical = candidates[0]['physical'] if size else None
        code = raw[physical:physical+size] if size else b''
        changed = projection.program(code, kind, name, shader_owners[name])
        if changed != code:
            updated = bytearray(header)
            struct.pack_into('<II', updated, 32, len(changed), zlib.crc32(changed))
            edits[at], changed_types[at] = (bytes(updated), None), 'shader_header'
            edits[physical], changed_types[physical] = (changed, None), 'shader_program'
    prepass_programs = {row['name'] for row in projection.rows if row.get('prepass_edits')}
    require(len(prepass_programs) == 8, 'Prepass pixel program closure differs')
    owners = {asset['root']:asset['name'] for asset in archive.assets if asset['type'] == 18}
    attachments, preserved_states = [], []
    for technique in archive.techniques:
        at, resident = technique['physical'], technique['resident']
        header = raw[at:at+184]
        kind = struct.unpack_from('<I', header, 8)[0]
        owner = owners[technique['asset']]
        plan = plan_by_type[(owner, kind)]
        state, blend = struct.unpack_from('<QQ', header, 160)
        preserved_states.append(dict(techset=owner, type=kind, state=hex(state)))
        if kind not in COLOR_PREPASS_TYPES:
            continue
        require(plan['name'].startswith('TECHNIQUE_DEPTH_PREPASS') and plan['shaders'][3] in prepass_programs,
                'Color prepass ownership differs')
        require(blend == struct.unpack_from('<Q', bytes.fromhex(plan['header']), 160)[0],
                'Prepass write mask differs from the authentic source')
        count = header[131]
        physical = archive.locate(archive.q(resident+136))
        require(reads[physical]['caller'] == 0xE0D118 and reads[physical]['size'] == count*5,
                'Format rows lack their exact native owner')
        before = raw[physical:physical+count*5]
        expected = bytes(v if v < 14 else v-2 for v in bytes.fromhex(plan['arrays']['formats']))
        require(before == expected, 'Prepass formats differ from the reviewed enum conversion')
        after = projection.prepass.formats(before)
        new_blend = projection.prepass.blend_bits(blend)
        updated = bytearray(header)
        struct.pack_into('<Q', updated, 168, new_blend)
        require(physical not in edits or edits[physical] == (after, None), 'Incompatible shared format array')
        edits[at], changed_types[at] = (bytes(updated), None), 'prepass_write_mask'
        edits[physical], changed_types[physical] = (after, None), 'prepass_attachments'
        attachments.append(dict(techset=owner, type=kind, technique=at, physical=physical,
                                before_formats=before.hex(), formats=after.hex(),
                                before_blend=hex(blend), blend=hex(new_blend), state=hex(state)))
    require(len(attachments) == 30, 'Expected all 30 colored prepasses')
    trace = archive.report()
    candidate, maxima = rebuild(raw, trace, edits)
    verified, final_trace = native_walk(ReplayArchive, candidate, output, 'renderer-rebuilt', 31)
    require(len(trace['reads']) == len(final_trace['reads']), 'Renderer changed typed read ordering')
    pointer_bytes = {i for ref in trace['refs'] for i in range(ref['physical'], ref['physical']+8)}
    changes = []
    for old, new in zip(trace['reads'], final_trace['reads']):
        require((old['caller'], old['block'], old['asset']) == (new['caller'], new['block'], new['asset']),
                'Renderer changed a native allocation owner')
        at = old['physical']
        expected = edits.get(at, (raw[at:at+old['size']], None))[0]
        actual = candidate[new['physical']:new['physical']+new['size']]
        require(len(expected) == len(actual) and
                all(a == b or at+i in pointer_bytes for i, (a,b) in enumerate(zip(expected, actual))),
                'Renderer changed bytes outside its admitted typed edits and relocated pointers')
        if at in edits:
            changes.append(dict(kind=changed_types[at], old_physical=at, physical=new['physical'],
                                before_bytes=old['size'], bytes=new['size'],
                                before_sha256=sha(raw[at:at+old['size']]), sha256=sha(actual)))
    final_owners = {asset['root']:asset['name'] for asset in verified.assets if asset['type'] == 18}
    final_states = [dict(techset=final_owners[t['asset']],
                         type=struct.unpack_from('<I',candidate,t['physical']+8)[0],
                         state=hex(struct.unpack_from('<Q',candidate,t['physical']+160)[0]))
                    for t in verified.techniques]
    require(final_states == preserved_states, 'Renderer changed a depth/stencil/cull word')
    del verified
    gc.collect()
    totals = dict(sum((Counter(row.get('renderer_edits', {})) for row in projection.rows), Counter()))
    receipt = dict(policy='island-six-vehicle-families-renderer-abi-v1',
                   before_sha256=sha(raw), sha256=sha(candidate), programs=projection.rows,
                   evidence_sha256=EVIDENCE_HASHES, renderer_edits=totals,
                   changed_programs=sum(row['changed'] for row in projection.rows),
                   prepass_programs=8, prepass_techniques=30, attachments=attachments,
                   typed_changes=changes, unknown_operands=0, all61_depth_stencil_cull_words_preserved=True,
                   material_local_buffers_untouched=True, unrelated_typed_bytes_preserved=True,
                   scene_fallbacks='Existing Island authored sky fields 196/197 and neutral FOV field 221',
                   entity_fallbacks='Existing Source identity atlas scales and no-animation frame words',
                   live_rendering_verified=False)
    write_json(output/'renderer-projection.json', receipt)
    return candidate, receipt
