"""Run in the matching IDA database; export portable candidates, never patch IDA.

Call export(input_json, output_json). Dependencies are IDA's bundled modules.
Entry signatures stay within a function. Short/ambiguous functions and globals
can use direct call or RIP-relative xrefs, resolved through the matched operand.
"""
import hashlib
import json
import struct
from pathlib import Path
import idaapi
import ida_bytes
import ida_funcs
import ida_nalt
import ida_segment
import ida_ua
import idautils
import idc


def export(input_path, output_path):
    inventory = json.loads(Path(input_path).read_text(encoding='utf-8-sig'))
    base = idaapi.get_imagebase()
    segments = [ida_segment.getseg(ea) for ea in idautils.Segments()]
    code = [(s.start_ea, ida_bytes.get_bytes(s.start_ea, s.end_ea-s.start_ea))
            for s in segments if s.perm & ida_segment.SEGPERM_EXEC]
    if not code or any(data is None for _, data in code):
        raise ValueError('Executable segment bytes unavailable')
    low, high = min(s.start_ea for s in segments), max(s.end_ea for s in segments)
    def pattern_bytes(ea):
        ins = ida_ua.insn_t()
        length = ida_ua.decode_insn(ins, ea)
        if not length:
            return None
        raw = ida_bytes.get_bytes(ea, length)
        if raw is None:
            return None
        mask = [True]*length
        ops = [o for o in ins.ops if o.type]
        # Mask address/control-flow operands and memory displacements (including
        # stack/member offsets). Keep ordinary immediates/opcodes/registers.
        for op in ops:
            if not op.offb:
                continue
            address_imm = op.type == ida_ua.o_imm and low <= op.value < high
            if op.type not in (ida_ua.o_near, ida_ua.o_far, ida_ua.o_mem, ida_ua.o_displ) and not address_imm:
                continue
            end = min([other.offb for other in ops if other.offb > op.offb] + [length])
            for n in range(op.offb, end):
                mask[n] = False
        return ins, raw, mask
    def unique(raw, mask):
        runs = []
        start = 0
        while start < len(raw):
            if not mask[start]:
                start += 1
                continue
            end = start+1
            while end < len(raw) and mask[end]:
                end += 1
            runs.append((start, bytes(raw[start:end])))
            start = end
        if not runs:
            return None
        offset, anchor = max(runs, key=lambda row: len(row[1]))
        if len(anchor) < 3:
            return None
        found = None
        for segment, data in code:
            at = data.find(anchor)
            while at >= 0:
                candidate = at-offset
                if candidate >= 0 and candidate+len(raw) <= len(data) and all(data[candidate+n:candidate+n+len(value)] == value for n, value in runs):
                    if found is not None:
                        return None
                    found = segment+candidate
                at = data.find(anchor, at+1)
        return found
    def window(start, end, minimum=32, maximum=160):
        raw, mask = bytearray(), []
        ea = start
        while ea < end and len(raw) < maximum:
            decoded = pattern_bytes(ea)
            if not decoded:
                break
            ins, chunk, fixed = decoded
            if ea+len(chunk) > end or len(raw)+len(chunk) > maximum:
                break
            raw.extend(chunk); mask.extend(fixed); ea += len(chunk)
            if len(raw) >= minimum and unique(raw, mask) == start:
                return ' '.join(f'{b:02X}' if keep else '??' for b, keep in zip(raw, mask))
            if idaapi.is_ret_insn(ins):
                break
        # Tiny function patterns are accepted only when unique in executable
        # sections. Otherwise use a larger caller context, not adjacent functions.
        if len(raw) >= 6 and unique(raw, mask) == start:
            return ' '.join(f'{b:02X}' if keep else '??' for b, keep in zip(raw, mask))
        return None
    def xref_signature(target):
        refs = []
        for ref in idautils.XrefsTo(target):
            segment = ida_segment.getseg(ref.frm)
            if segment and segment.perm & ida_segment.SEGPERM_EXEC:
                refs.append(ref.frm)
        for ea in sorted(set(refs))[:32]:
            decoded = pattern_bytes(ea)
            func = ida_funcs.get_func(ea)
            if not decoded or not func:
                continue
            ins, raw, _ = decoded
            displacement = None
            for op in ins.ops:
                if op.type not in (ida_ua.o_near, ida_ua.o_mem, ida_ua.o_displ) or not op.offb or op.offb+4 > len(raw):
                    continue
                rel = struct.unpack_from('<i', raw, op.offb)[0]
                if ea+ins.size+rel == target:
                    displacement = op.offb
                    break
            if displacement is None:
                continue
            # Include up to 24 bytes of decoded context before the operand so
            # call-at-end functions can still form a useful signature.
            starts = [ea]
            heads = [h for h in idautils.Heads(max(func.start_ea, ea-32), ea) if ida_bytes.is_code(ida_bytes.get_full_flags(h))]
            starts += list(reversed(heads))
            for start in starts:
                signature = window(start, func.end_ea)
                if signature and len(signature.split()) >= ea-start+ins.size:
                    return {'pattern': signature, 'anchor_rva': hex(start-base),
                            'resolve': {'kind': 'rel32', 'offset': ea-start+displacement, 'next': ea-start+ins.size}}
        return None

    results = []
    for source in inventory['symbols']:
        rva = int(source['rva'], 0); ea = base+rva
        result = {'name': source['name'], 'source_rva': hex(rva), 'aliases': source.get('aliases', []),
                  'sources': source.get('sources', []), 'section': 'executable', 'ida_name': idc.get_name(ea)}
        segment = ida_segment.getseg(ea)
        if not segment:
            result['reason'] = 'address is not mapped in this IDB'
            results.append(result); continue
        is_code = bool(segment.perm & ida_segment.SEGPERM_EXEC)
        result['target'] = 'executable' if is_code else 'readable'
        func = ida_funcs.get_func(ea)
        decoded = pattern_bytes(ea) if is_code else None
        # Dumps made with active hooks can start with a branch to an external
        # trampoline. Such bytes are not an engine signature.
        external_jump = decoded and decoded[0].get_canon_mnem() == 'jmp' and any(
            o.type == ida_ua.o_near and not low <= o.addr < high for o in decoded[0].ops)
        prefix = source.get('expected_prefix')
        mismatch = prefix and ida_bytes.get_bytes(ea, len(prefix.split())) != bytes.fromhex(prefix)
        if external_jump or mismatch:
            result['reason'] = 'dump entry is patched or differs from verified hook bytes; obtain a clean dump'
        else:
            signature = window(ea, func.end_ea) if is_code and func and func.start_ea == ea else None
            if signature:
                result.update(pattern=signature, anchor_rva=hex(rva), resolve={'kind': 'match'})
            else:
                candidate = xref_signature(ea)
                if candidate:
                    result.update(candidate)
                else:
                    result['reason'] = 'no unique bounded function/call/RIP-reference signature; manual semantic mapping required'
        results.append(result)
    document = {'schema': 1, 'build': inventory.get('build'), 'input': ida_nalt.get_input_file_path(),
                'input_sha256': ida_nalt.retrieve_input_file_sha256().hex(), 'ida_base': hex(base),
                'notice': 'Unique source candidates, not ABI proof. Missing and patched symbols stay unresolved.', 'symbols': results}
    target = Path(output_path); target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(document, indent=2)+'\n', encoding='utf-8')
    ready = sum('pattern' in r for r in results)
    print(json.dumps({'output': str(target), 'total': len(results), 'patterns': ready, 'unavailable': len(results)-ready}))
    return document
