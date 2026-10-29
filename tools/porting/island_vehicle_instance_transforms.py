"""Two authenticated Source vertex transforms projected onto Replay CB0.

Pure byte transformation: no filesystem, process or graphics access. The two
programs retain their Source lighting calculations and exact signatures. Replay
stores four matrix rows per instance, while Source consumes a quaternion and a
translation/scale pair. Merely doubling the index does not fix that contract.
"""
import hashlib
import struct

from dxbc import chunks, instructions, operation, reg, retail_hash


PROFILES = {
    '86247b3b286c61e760c94cbe3c728f7b': {
        'input': '0dba5a46107f5fccda9759ac3d2f7cdbd2b5f7ef6f63fcbea4b17c6a534f4022',
        'output': '1f3e81e5e511b32c0d612a39f86913257ee5fcf67e0c6bb907ce2a1b2498ba70',
        'span': 'a84ef8a1507ed952fef0e34c6ca0585dae0ee2991b9a0e5b08602c8e7655ea96',
        'origin': 5,
    },
    'be81c7be9e360bef70806df75d88ecb4': {
        'input': 'd0046915301236f2428d6f39c509bf84fdfa9c3e31f76a0f45529661b86438c0',
        'output': '2c71a11e6ed50e2e66405073ec587dbcefb0d125276f240058610356f5f86792',
        'span': '65aa9c5062fb3eff09e007ca731b1f7d589c9ed9c3572a59858ef35288346c56',
        'origin': 6,
    },
}
LANES = 'xyzw'
BEGIN, END = 258, 299
DECL = [67111001, 2133574, 0, 258]
TEMPS = [33554536, 14]
SHIFT = [170393641, 1048658, 0, 1049094, 0, 16386, 3, 0, 1, 0]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def packed(words):
    return struct.pack('<%dI' % len(words), *words)


def vector(index, swizzle='xyzw', kind=0):
    if len(swizzle) == 1:
        swizzle *= 4
    require(len(swizzle) == 4 and all(c in LANES for c in swizzle), 'Invalid source lanes')
    return [2 | 4 | (kind << 12) | (1 << 20) |
            sum(LANES.index(c) << (4 + 2 * i) for i, c in enumerate(swizzle)), index]


def destination(index, mask, kind=0):
    require(mask and len(set(mask)) == len(mask) and all(c in LANES for c in mask), 'Invalid destination lanes')
    return [2 | (kind << 12) | (1 << 20) | sum(1 << (4 + LANES.index(c)) for c in mask), index]


def constant(offset, swizzle='xyzw', register=0, dynamic=True, negative=False):
    token = vector(0, swizzle)[0]
    token = (token & ~(3 << 20)) | (8 << 12) | (2 << 20)
    if dynamic:
        token |= 3 << 25
    result = [token, register, offset]
    if dynamic:
        require(register == 0, 'Only the authenticated instance index may be dynamic')
        result += reg(0, 2)
    if negative:
        result[0] |= 0x80000000
        result.insert(1, 0x41)
    return result


def scalar(value):
    return [0x4001, struct.unpack('<I', struct.pack('<f', value))[0]]


def matrix_block(origin):
    """Preserve the live Source register interface using four new temporaries.

    Input r0.x/y/w is local position; r0.z is the matrix-row base (4*instance).
    r6.xyz and r5.xyz are normal/tangent; r2.x/y/w is self-visibility direction,
    and r2.z is its independent amplitude. Output r1 is camera-relative position,
    r0.x/y/w the transformed normal, o3.xyz tangent, and r2.x/y/w transformed
    self-visibility direction. Source's following normalization and SH code stay
    byte-exact. r14 holds local position; r15..17 hold the three matrix rows.
    """
    out = []

    def emit(opcode, index, mask, *args, precise=False, output=False):
        flags = sum(1 << (19 + LANES.index(c)) for c in mask) if precise else 0
        out.extend(operation(opcode | flags, destination(index, mask, 2 if output else 0), *args))

    emit(54, 14, 'xyz', vector(0, 'xywx'), precise=True)
    emit(54, 14, 'w', scalar(1.0), precise=True)
    for row in range(3):
        emit(54, 15 + row, 'xyzw', constant(2 + row), precise=True)
        emit(30, 1, LANES[row], constant(origin, LANES[row], 2, False, True),
             vector(15 + row, 'w'), precise=True)
    emit(43, 1, 'xyz', vector(1, 'xyzx'), precise=True)
    emit(14, 1, 'xyz', vector(1, 'xyzx'), scalar(4096.0), precise=True)
    for row in range(3):
        emit(54, 15 + row, 'w', vector(1, LANES[row]), precise=True)
    for row in range(3):
        emit(17, 1, LANES[row], vector(14), vector(15 + row), precise=True)
    emit(17, 1, 'w', vector(14), constant(5), precise=True)

    # Replay removes uniform scale using the length of the first matrix column.
    # Using the exact native basis also preserves negative scale/reflection.
    for row in range(3):
        emit(54, 3, LANES[row], vector(15 + row, 'x'), precise=True)
    emit(16, 3, 'w', vector(3, 'xyzx'), vector(3, 'xyzx'))
    emit(68, 3, 'w', vector(3, 'w'))
    for row in range(3):
        emit(56, 15 + row, 'xyz', vector(3, 'w'), vector(15 + row, 'xyzx'))
    for row, lane in enumerate('xyw'):
        emit(16, 0, lane, vector(6, 'xyzx'), vector(15 + row, 'xyzx'))
    for row in range(3):
        emit(16, 3, LANES[row], vector(5, 'xyzx'), vector(15 + row, 'xyzx'), output=True)
    for row in range(3):
        emit(16, 3, LANES[row], vector(2, 'xywx'), vector(15 + row, 'xyzx'))
    emit(54, 2, 'xyw', vector(3, 'xyxz'))
    for row in range(4):
        emit(17, 0, LANES[row], vector(1), constant(row, register=2, dynamic=False),
             precise=True, output=True)
    return out


def rewrite(code, name):
    require(name in PROFILES, 'Shader has no admitted compact-transform profile')
    profile = PROFILES[name]
    digest = sha(code)
    if profile['output'] and digest == profile['output']:
        return code, {'name': name, 'changed': False, 'sha256': digest, 'idempotent': True}
    require(digest == profile['input'], 'Complete Source shader fingerprint differs')
    require(code[4:20] == retail_hash(code[20:]), 'Input DXBC checksum differs')
    parts = chunks(code)
    require(len({tag for tag, _ in parts}) == len(parts), 'Duplicate DXBC chunk')
    require(b'RDEF' not in dict(parts), 'Reflection requires an explicit migration')
    shader_parts = [(tag, body) for tag, body in parts if tag in (b'SHEX', b'SHDR')]
    require(len(shader_parts) == 1, 'Expected exactly one instruction chunk')
    tag, body = shader_parts[0]
    words = list(struct.unpack('<%dI' % (len(body) // 4), body))
    require(words[0] == 0x10050, 'Expected vertex shader model 5.0')
    parsed = list(instructions(words))
    require(len(parsed) == 456, 'Instruction inventory differs')
    require(parsed[3] == DECL and parsed[28] == TEMPS and parsed[53] == SHIFT, 'Declaration/index guards differ')
    require(sha(packed(sum(parsed[BEGIN:END], []))) == profile['span'], 'Transform instruction span differs')
    replacement = matrix_block(profile['origin'])
    replacement_instructions = list(instructions([words[0], len(replacement) + 2] + replacement))
    require(all(ins[0] & 0x7ff in (14, 16, 17, 30, 43, 54, 56, 68) for ins in replacement_instructions),
            'Replacement must remain an arithmetic-only basic block')
    before = [ins[:] for ins in parsed]
    parsed[3][-1] = 514
    parsed[28][-1] = 18
    parsed[53][8] = 2
    new_instructions = parsed[:BEGIN] + replacement_instructions + parsed[END:]
    output_words = words[:2] + sum(new_instructions, [])
    output_words[1] = len(output_words)
    output_body = packed(output_words)
    require(len(list(instructions(output_words))) == len(new_instructions), 'Invalid emitted instruction extents')
    require(all(before[i] == parsed[i] for i in range(len(parsed)) if i not in (3, 28, 53)),
            'Unrelated instruction changed')
    output_parts = [(kind, output_body if kind == tag else data) for kind, data in parts]
    output = bytearray(code[:32] + bytes(4 * len(parts)))
    for index, (kind, data) in enumerate(output_parts):
        struct.pack_into('<I', output, 32 + 4 * index, len(output))
        output.extend(kind + struct.pack('<I', len(data)) + data)
    struct.pack_into('<I', output, 24, len(output))
    output[4:20] = retail_hash(output[20:])
    output = bytes(output)
    require([(kind, data) for kind, data in chunks(output) if kind != tag] ==
            [(kind, data) for kind, data in parts if kind != tag], 'Signature or unrelated chunk changed')
    if profile['output']:
        require(sha(output) == profile['output'], 'Projected shader fingerprint differs')
    return output, dict(name=name, changed=True, input_sha256=digest, sha256=sha(output),
        input_bytes=len(code), bytes=len(output), source_span_sha256=profile['span'],
        source_instructions=END - BEGIN, matrix_instructions=len(replacement_instructions),
        index_instruction=53, instance_stride_rows=[2, 4], cb0_extent=[258, 514],
        temporary_extent=[14, 18], temporary_registers=[14, 15, 16, 17],
        camera_origin_cb2_index=profile['origin'], signatures_unchanged=True,
        source_shader_prefix_suffix_preserved=True, arithmetic_only_replacement=True,
        exact_unchanged_chunks=[kind.decode('ascii') for kind, _ in parts if kind != tag])
