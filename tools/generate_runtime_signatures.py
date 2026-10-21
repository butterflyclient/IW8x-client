"""Generate the pinned Replay runtime signature registry without changing bindings.

Usage: python tools/generate_runtime_signatures.py --image PATH [--report PATH]
The executable SHA, each catalog pattern's uniqueness, and its exact resolution
are required. Source additions prove byte positions, not new callable ABIs.
Only explicit callable/guard/address declarations are inventoried; arbitrary
hexadecimal constants, log strings, comments, and data RVAs are not signatures.
"""

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


class image:
    def __init__(self, path):
        self.bytes = path.read_bytes()
        self.sha256 = hashlib.sha256(self.bytes).hexdigest()
        if self.bytes[:2] != b'MZ':
            raise ValueError('Not an MZ image')
        pe = struct.unpack_from('<I', self.bytes, 0x3C)[0]
        if self.bytes[pe:pe + 4] != b'PE\0\0':
            raise ValueError('Not a PE image')
        machine, count = struct.unpack_from('<HH', self.bytes, pe + 4)
        opt_size = struct.unpack_from('<H', self.bytes, pe + 20)[0]
        opt = pe + 24
        if machine != 0x8664 or struct.unpack_from('<H', self.bytes, opt)[0] != 0x20B:
            raise ValueError('Expected AMD64 PE32+')
        self.base = struct.unpack_from('<Q', self.bytes, opt + 24)[0]
        self.sections = []
        for index in range(count):
            start = opt + opt_size + index * 40
            name = self.bytes[start:start + 8].rstrip(b'\0').decode('ascii')
            virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', self.bytes, start + 8)
            flags = struct.unpack_from('<I', self.bytes, start + 36)[0]
            length = min(raw_size, virtual_size or raw_size)
            if raw + raw_size > len(self.bytes):
                raise ValueError(f'Truncated section {name}')
            self.sections.append({'name': name, 'rva': rva, 'flags': flags,
                                  'virtual_size': virtual_size or raw_size,
                                  'bytes': self.bytes[raw:raw + length]})

    def section(self, rva, size=1, backed=False):
        return next((s for s in self.sections
                     if s['rva'] <= rva and rva + size <= s['rva'] +
                     (len(s['bytes']) if backed else s['virtual_size'])), None)

    def read(self, rva, size):
        section = self.section(rva, size, backed=True)
        if section is None:
            raise ValueError(f'Operand outside backed image: 0x{rva:X}')
        offset = rva - section['rva']
        return section['bytes'][offset:offset + size]

    def text(self, rva):
        section = self.section(rva, backed=True)
        return section is not None and section['name'] == '.text' and bool(section['flags'] & 0x20000000)


def parse_pattern(pattern):
    values, masks = [], []
    for token in pattern.split():
        if token == '?':
            token = '??'
        if not re.fullmatch(r'[0-9A-Fa-f?]{2}', token):
            raise ValueError(f'Invalid signature token: {token}')
        value = mask = 0
        for nibble in token:
            value <<= 4
            mask <<= 4
            if nibble != '?':
                value |= int(nibble, 16)
                mask |= 15
        values.append(value)
        masks.append(mask)
    if not values or len(values) > 4096 or not any(masks):
        raise ValueError('Empty, too long, or all-wildcard signature')
    return bytes(values), bytes(masks)


def find_matches(executable, pattern, selected='executable'):
    values, masks = parse_pattern(pattern)
    runs = []
    start = 0
    while start < len(masks):
        if masks[start] != 255:
            start += 1
            continue
        end = start + 1
        while end < len(masks) and masks[end] == 255:
            end += 1
        runs.append((start, end))
        start = end
    if not runs:
        raise ValueError('Registry patterns need an exact byte anchor')
    anchor_start, anchor_end = max(runs, key=lambda pair: pair[1] - pair[0])
    anchor = values[anchor_start:anchor_end]
    found = []
    for section in executable.sections:
        flags = section['flags']
        if not ((selected == 'executable' and flags & 0x20000000)
                or (selected == 'readable' and flags & 0x40000000)
                or selected == section['name']):
            continue
        data, at = section['bytes'], 0
        while True:
            hit = data.find(anchor, at)
            if hit < 0:
                break
            at = hit + 1
            candidate = hit - anchor_start
            if candidate < 0 or candidate + len(values) > len(data):
                continue
            if all((data[candidate + i] & mask) == value
                   for i, (value, mask) in enumerate(zip(values, masks))):
                found.append(section['rva'] + candidate)
                if len(found) == 2:
                    return found
    return found


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def resolve(executable, anchor, rule):
    kind = rule.get('kind', 'match')
    target = anchor + number(rule.get('offset', 0))
    if kind == 'rel32':
        displacement = struct.unpack('<i', executable.read(target, 4))[0]
        target = anchor + number(rule.get('next', 0)) + displacement
    elif kind == 'abs64':
        target = struct.unpack('<Q', executable.read(target, 8))[0] - executable.base
    elif kind != 'match':
        raise ValueError(f'Unsupported resolver kind: {kind}')
    return target + number(rule.get('add', 0))


def verify_entry(executable, entry):
    hits = find_matches(executable, entry['pattern'], entry.get('section', 'executable'))
    if len(hits) != 1:
        reason = 'missing' if not hits else 'ambiguous'
        raise ValueError(f'{entry["name"]}: {reason} signature ({len(hits)} matches, capped at 2)')
    target = resolve(executable, hits[0], entry.get('resolve', {}))
    expected = number(entry['source_rva'])
    if target != expected:
        raise ValueError(f'{entry["name"]}: resolved 0x{target:X}, expected 0x{expected:X}')
    section = executable.section(target)
    expectation = entry.get('target', 'executable')
    permission = 0x20000000 if expectation == 'executable' else 0x40000000
    if section is None or not section['flags'] & permission:
        raise ValueError(f'{entry["name"]}: resolved target permissions mismatch')


def scrub_source(text):
    # Preserve positions/newlines for evidence; never inventory comments/log strings.
    token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    return token.sub(lambda match: ''.join('\n' if c == '\n' else ' ' for c in match[0]), text)


def inventory_sources(project, executable):
    inventory, excluded = {}, []
    literal = r'(0x[0-9A-Fa-f]{5,})'
    patterns = [
        ('address_declaration', re.compile(
            r'\b(?:constexpr|const)\s+(?:std::)?uintptr_t\s+(\w+)\s*=\s*' + literal)),
        ('named_rva_declaration', re.compile(
            r'\b(?:constexpr|const)\s+(?:std::)?(?:uint32_t|uint64_t|size_t|auto)\s+'
            r'(\w*(?:RVA|Rva|_rva))\s*=\s*' + literal)),
        ('binding_bytes_row', re.compile(
            r'\{\s*' + literal + r'\s*,\s*(?:\{\s*0x|(?:guard\w+|\w*(?:Bytes|Prologue|_entry|_bytes))\b)')),
        ('literal_hook', re.compile(
            r'(?:hook::(?:Install|detail::Prepare)|\b\w+\.Add)\s*\('
            r'[^;\n]{0,240}?\+\s*' + literal)),
        ('literal_guard', re.compile(
            r'\b(?:Matches|MatchesTarget|Match|matches|VerifyBinding|signature)\s*\(\s*' + literal)),
        ('hook_macro', re.compile(r'\b\w+_HOOK\s*\(\s*' + literal)),
        ('native_wrapper', re.compile(
            r'\b(?:Native|native|fn|Fn|Function|function)\s*<[^;\n]{1,240}?>\s*\(\s*' + literal)),
        ('function_pointer_cast', re.compile(
            r'reinterpret_cast\s*<[^;\n]{1,200}?\(\s*\*\s*\)[^;\n]{0,120}?>\s*\('
            r'[^;\n]{0,80}?\+\s*' + literal)),
        ('encoded_address_reference', re.compile(r'\brva_([0-9a-fA-F]{8})\b')),
    ]
    for path in sorted((project / 'src').rglob('*')):
        if path.suffix not in ('.cpp', '.h', '.hpp', '.inc') or 'tests' in path.parts:
            continue
        relative = path.relative_to(project).as_posix()
        code = scrub_source(path.read_text(encoding='utf-8-sig', errors='replace'))
        for classification, regex in patterns:
            for match in regex.finditer(code):
                groups = match.groups()
                rva = int(groups[-1], 16)
                name = groups[0] if classification.endswith('declaration') else None
                evidence = {'path': relative, 'line': code.count('\n', 0, match.start()) + 1,
                            'classification': classification}
                if name:
                    evidence['name'] = name
                if not executable.text(rva):
                    excluded.append({'rva': f'0x{rva:X}', **evidence, 'reason': 'outside backed executable .text'})
                    continue
                sites = inventory.setdefault(rva, [])
                if evidence not in sites:
                    sites.append(evidence)
    return inventory, excluded


def exact_entry(executable, rva, sources):
    section = executable.section(rva)
    available = min(128, section['rva'] + len(section['bytes']) - rva)
    # These are pinned-build exact bytes; rel32 operands need no ABI reinterpretation.
    # Extend ambiguous entry bytes instead of choosing the first match.
    for length in range(32, available + 1, 8):
        raw = executable.read(rva, length)
        pattern = ' '.join(f'{byte:02X}' for byte in raw)
        hits = find_matches(executable, pattern)
        if hits == [rva]:
            names = sorted({site['name'] for site in sources if site.get('name')})
            name = names[0] if names else f'runtime_site_{rva:08x}'
            return {'name': name, 'source_rva': f'0x{rva:X}', 'pattern': pattern,
                    'section': 'executable', 'resolve': {'kind': 'match'},
                    'target': 'executable', 'aliases': names, 'source_sites': sources,
                    'origin': 'current_source_exact_position'}
    raise ValueError(f'No unique 32..128-byte signature for source code position 0x{rva:X}')


def cpp_string(value):
    return json.dumps(str(value), ensure_ascii=True)


def write_if_changed(path, content):
    encoded = content.encode('utf-8')
    if path.exists() and path.read_bytes() == encoded:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    project = args.project.resolve()
    manifest_path = args.manifest or project / 'data/signatures/replay-tu19.json'
    output_path = args.output or project / 'data/signatures/runtime_registry.inc'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    executable = image(args.image)
    if executable.sha256 != manifest['input_sha256']:
        raise ValueError(f'Unsupported executable SHA256={executable.sha256}')
    entries = manifest['symbols']
    names, covered = set(), set()
    for entry in entries:
        if entry['name'] in names:
            raise ValueError(f'Duplicate registry name: {entry["name"]}')
        names.add(entry['name'])
        verify_entry(executable, entry)
        covered.add(number(entry['source_rva']))
    inventory, excluded = inventory_sources(project, executable)
    added = []
    for rva, sources in sorted(inventory.items()):
        if rva in covered:
            continue
        entry = exact_entry(executable, rva, sources)
        if entry['name'] in names:
            entry['name'] = f'{entry["name"]}_{rva:08x}'
        names.add(entry['name'])
        verify_entry(executable, entry)
        added.append(entry)
        covered.add(rva)
    registry = sorted(entries + added, key=lambda entry: (number(entry['source_rva']), entry['name']))
    if not any(number(entry['source_rva']) == 0x12BB850 for entry in registry):
        raise ValueError('Required kNetchanTransmitRVA source declaration was not inventoried')
    lines = ['// Generated by tools/generate_runtime_signatures.py; do not edit.',
             '// Pinned Replay SHA256: ' + executable.sha256,
             '// Exact source positions; native ABIs/layouts remain independently verified.',
             '// IW8X_RUNTIME_SIGNATURE(name,rva,pattern,section,kind,offset,next,add,target,aliases)']
    for entry in registry:
        rule = entry.get('resolve', {})
        if entry.get('source_sites'):
            lines.append('// Source additions: ' + '; '.join(
                f'{site["path"]}:{site["line"]} ({site["classification"]})'
                for site in entry['source_sites']))
        fields = [cpp_string(entry['name']), f'0x{number(entry["source_rva"]):08X}',
                  cpp_string(entry['pattern']), cpp_string(entry.get('section', 'executable')),
                  rule.get('kind', 'match'), str(number(rule.get('offset', 0))),
                  str(number(rule.get('next', 0))), str(number(rule.get('add', 0))),
                  cpp_string(entry.get('target', 'executable')),
                  cpp_string('|'.join(entry.get('aliases', [])))]
        lines.append('IW8X_RUNTIME_SIGNATURE(' + ', '.join(fields) + ')')
    write_if_changed(output_path, '\n'.join(lines) + '\n')
    report = {'schema': 1, 'image_sha256': executable.sha256,
              'manifest_entries': len(entries), 'source_additions': len(added),
              'registry_entries': len(registry), 'inventoried_text_positions': len(inventory),
              'uncovered_text_positions': sorted(set(inventory) - covered),
              'scan_proof': 'Every pattern uniquely matched and resolved to source_rva in the pinned file.',
              'limitations': ['Source inventory uses explicit address declarations/binding rows/hook and native-call syntax.',
                              'This is position coverage, not a new ABI/layout proof or a runtime acceptance test.',
                              'Address declarations outside backed .text remain data/layout contracts, not code signatures.'],
              'covered_sources': [{'rva': f'0x{rva:X}', 'origin': 'catalog' if rva in
                                   {number(entry['source_rva']) for entry in entries} else 'source_addition',
                                   'sites': sources} for rva, sources in sorted(inventory.items())],
              'additions': added, 'excluded': excluded}
    if args.report:
        write_if_changed(args.report, json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('manifest_entries', 'source_additions',
                     'registry_entries', 'inventoried_text_positions', 'uncovered_text_positions')}))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, struct.error) as error:
        raise SystemExit(str(error)) from error
