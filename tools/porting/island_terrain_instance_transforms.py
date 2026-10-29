"""Project one authenticated Island terrain color VS onto Replay's matrix CB0.

The Source 1.64 VS f283... uses two compact quaternion/translation rows per
instance, while the Replay 1.20 prepass and its native renderer use four matrix
rows. The main EQUAL depth pass therefore computes different clip positions.

This tool changes exactly the instance-transform arithmetic and two declaration
extents. It does not change material, pixel shader, depth, blend, or sampler
state. This is a pure bytecode transformation; no game/FF/process access.
"""
from __future__ import annotations

import hashlib
import struct

from dxbc import chunks, instructions, operation, retail_hash
from island_vehicle_instance_transforms import (
    constant, destination, reg, scalar, vector,
)


PROFILE = {
    'name': 'f28387a29d6ef7a523c5d9f21d79f360',
    'input_sha256': '358a70627a780343c410edb75570869b07f5ad9fd93f014e5793c9c322b946f3',
    'input_bytes': 18220,
    'source_span_sha256': '0ca1a2467ffd73fdc78b64ee6a7ad4bae5e09bfd2b08b6ab291566fa7d66176b',
    'source_instructions': 558,
    'begin': 173,
    'end': 204,
    'cb0_declaration': [0x4000859, 0x208e46, 0, 258],
    'temps_declaration': [0x2000068, 18],
    'cb2_camera_origin': 5,
}

LANES = 'xyzw'


def require(predicate: bool, message: str) -> None:
    if not predicate:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def packed(words: list[int]) -> bytes:
    return struct.pack('<%dI' % len(words), *words)


def indexed_matrix_row(offset: int) -> list[int]:
    """Change only the dynamic index operand from vehicle r0.z to terrain r0.x."""
    words = constant(offset)
    require(words[-2:] == reg(0, 2), 'Matrix address does not use the pinned temporary index')
    words[-2:] = reg(0, 0)
    return words


def matrix_transform() -> list[int]:
    """Match Replay's matrix-row basis while retaining terrain Source outputs.

    On entry: r0.y instance number, r1.x/z/w local vertex position, r7.xyz
    normal, r6.xyz tangent. On exit: r1.xyz camera-relative position, r2.xyz
    transformed normal and o4.xyz transformed tangent. The following Source
    instructions produce SV_POSITION from r1 and retain original lighting.
    Private r18 holds local position; r19/r20/r21 hold Replay matrix rows.
    """
    result: list[int] = []

    def emit(opcode: int, index: int, mask: str, *args: list[int],
             precise: bool = False, output: bool = False) -> None:
        flags = sum(1 << (19 + LANES.index(c)) for c in mask) if precise else 0
        result.extend(operation(opcode | flags,
                                destination(index, mask, 2 if output else 0), *args))

    # Source's subsequent projection expects r1; preserve its non-contiguous
    # local x/z/w lanes before temporarily using r1 for camera subtraction.
    emit(54, 18, 'xyz', vector(1, 'xzww'), precise=True)
    emit(54, 18, 'w', scalar(1.0), precise=True)

    # Replay's four-row matrix has 3 basis+translation rows starting at
    # cb0[4*instance+2]. Row.w is integer world translation in 1/4096 units.
    for row in range(3):
        emit(54, 19 + row, 'xyzw', indexed_matrix_row(2 + row), precise=True)
    for row, lane in enumerate('xyz'):
        emit(30, 1, lane,
             constant(5, lane, register=2, dynamic=False, negative=True),
             vector(19 + row, 'w'), precise=True)
    emit(43, 1, 'xyz', vector(1, 'xyzx'), precise=True)
    emit(14, 1, 'xyz', vector(1, 'xyzx'), scalar(4096.0), precise=True)
    for row, lane in enumerate('xyz'):
        emit(54, 19 + row, 'w', vector(1, lane), precise=True)
    for row, lane in enumerate('xyz'):
        emit(17, 1, lane, vector(18), vector(19 + row), precise=True)

    # Native Replay normal/tangent transforms remove the uniform matrix scale
    # before the basis product. Retain source pixel interface o4 unchanged.
    for row, lane in enumerate('xyz'):
        emit(54, 3, lane, vector(19 + row, 'x'), precise=True)
    emit(16, 3, 'w', vector(3, 'xyzx'), vector(3, 'xyzx'))
    emit(68, 3, 'w', vector(3, 'w'))
    for row in range(3):
        emit(56, 19 + row, 'xyz', vector(3, 'w'), vector(19 + row, 'xyzx'))
    for row, lane in enumerate('xyz'):
        emit(16, 2, lane, vector(7, 'xyzx'), vector(19 + row, 'xyzx'))
    for row, lane in enumerate('xyz'):
        emit(16, 4, lane, vector(6, 'xyzx'), vector(19 + row, 'xyzx'), output=True)
    # The matching Replay depth prepass transforms homogeneous W with CB0's
    # fourth matrix row. Retaining Source's following literal W=1 can make an
    # otherwise matching color pass fail its EQUAL depth comparison.
    emit(17, 1, 'w', vector(18), indexed_matrix_row(5), precise=True)
    return result


def rewrite(code: bytes, name: str):
    require(name == PROFILE['name'], 'Terrain shader identity is not allowlisted')
    require(len(code) == PROFILE['input_bytes'] and sha(code) == PROFILE['input_sha256'],
            'Complete installed terrain shader fingerprint differs')
    require(code[4:20] == retail_hash(code[20:]), 'Source DXBC checksum differs')
    parts = chunks(code)
    require(len({tag for tag, _ in parts}) == len(parts), 'Duplicate DXBC container chunk')
    require(b'RDEF' not in dict(parts), 'Shader contains reflection metadata requiring a separate conversion')
    instruction_chunks = [(tag, body) for tag, body in parts if tag in (b'SHEX', b'SHDR')]
    require(len(instruction_chunks) == 1, 'Expected exactly one vertex instruction stream')
    tag, body = instruction_chunks[0]
    require(len(body) % 4 == 0, 'Unaligned DXBC program')
    words = list(struct.unpack('<%dI' % (len(body) // 4), body))
    require(words[0] == 0x10050, 'Expected shader model vs_5_0')
    parsed = list(instructions(words))
    require(len(parsed) == PROFILE['source_instructions'], 'Terrain VS instruction inventory differs')
    require(parsed[3] == PROFILE['cb0_declaration'] and parsed[28] == PROFILE['temps_declaration'],
            'Source constant buffer or temporary declaration differs')
    begin, end = PROFILE['begin'], PROFILE['end']
    require(sha(packed(sum(parsed[begin:end], []))) == PROFILE['source_span_sha256'],
            'Exact Source compact-instance span differs')
    # Include Source's former "mov r1.w,1" in the guarded replaced span;
    # it must be replaced by the exact Replay fourth-row calculation above.
    require(parsed[end - 1][0] & 0x7ff == 54, 'Source homogeneous-W operation differs')
    # Keep the first instruction with its precise flag/operand encoding and
    # change the literal 1 (2-row shift) to 2 (4-row shift) only.
    shift = parsed[begin][:]
    require(shift[0] & 0x7ff == 41 and shift[-1] == 1,
            'Instance shift source instruction differs')
    shift[-1] = 2
    original = [row[:] for row in parsed]
    parsed[3][-1] = 514
    parsed[28][-1] = 22
    replacement = matrix_transform()
    expanded = list(instructions([words[0], len(replacement) + 2] + replacement))
    require(all(row[0] & 0x7ff in (14, 16, 17, 30, 43, 54, 56, 68)
                for row in expanded), 'Terrain replacement contains non-arithmetic VM operations')
    output_program = parsed[:begin] + [shift] + expanded + parsed[end:]
    require(all(row == original[i] for i, row in enumerate(parsed)
                if i not in (3, 28)), 'Unrelated instructions were rewritten')
    output_words = words[:2] + sum(output_program, [])
    output_words[1] = len(output_words)
    require(len(list(instructions(output_words))) == len(output_program),
            'Emitted program instruction boundaries invalid')
    encoded = packed(output_words)
    output_parts = [(kind, encoded if kind == tag else contents) for kind, contents in parts]
    output = bytearray(code[:32] + bytes(4 * len(parts)))
    for index, (kind, contents) in enumerate(output_parts):
        struct.pack_into('<I', output, 32 + 4 * index, len(output))
        output.extend(kind + struct.pack('<I', len(contents)) + contents)
    struct.pack_into('<I', output, 24, len(output))
    output[4:20] = retail_hash(output[20:])
    result = bytes(output)
    require(result[4:20] == retail_hash(result[20:]), 'Rebuilt terrain DXBC checksum incorrect')
    original_other = [(kind, contents) for kind, contents in parts if kind != tag]
    after_other = [(kind, contents) for kind, contents in chunks(result) if kind != tag]
    require(after_other == original_other, 'Shader signatures or other DXBC chunks changed')
    require(result != code, 'Terrain shader was not translated')
    receipt = dict(name=name, source_sha256=sha(code), sha256=sha(result), input_bytes=len(code),
                   bytes=len(result), source_instructions=len(parsed),
                   source_span_sha256=PROFILE['source_span_sha256'],
                   source_span_indices=[begin, end], emitted_transform_instructions=len(expanded),
                   source_instance_rows=2, replay_instance_rows=4, cb0_rows=[258, 514],
                   temporary_count=[18, 22], output_geometry_abi='four matrix rows + normalized basis',
                   source_shading_chunks_unchanged=True, depth_state_unchanged=True)
    return result, receipt
